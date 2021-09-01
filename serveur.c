#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define CMD "serveur"
#define NB_WORKERS 10

typedef struct // structure d'une requete du systeme client serveur
{
  char action[10];
  char directorie[1024];
  char name[1024];
  off_t size;
  struct timespec time;
} Request;

typedef struct DataSpec_t
{
  pthread_t id; /* identifiant du thread */
  int libre;    /* indicateur de terminaison */
                /* ajouter donnees specifiques après cette ligne */
  int tid;      /* identifiant logique */
  int canal;    /* canal de communication */
  sem_t sem;    /* semaphore de reveil */
} DataSpec;

typedef struct DataThread_t
{
  DataSpec spec;             /* donnees specifiques : voir dataspec.h */
  struct DataThread_t *next; /* si thread suivant, alors son adresse, 
                                 sinon NULL */
} DataThread;

typedef struct // structure permettant de savoir quels dossiers sont en cours d'utilisation et de les protéger par mutex
{
  char directorie[1024];
  int bool;
  pthread_mutex_t mutex;
} visited;

int ecoute;
char answer[1024];
sem_t semWorkersLibres;
pthread_mutex_t mutexCanal[NB_WORKERS];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
visited tabvisited[1024];
DataSpec dataSpec[NB_WORKERS];

void creerCohorteWorkers(void);
int chercherWorkerLibre(void);
void *threadWorker(void *arg);
void lockMutexCanal(int numWorker);
void unlockMutexCanal(int numWorker);
void sig_handler(int sig);
void _mkdir(const char *dir);
void ask(int sock, Request *fromclient, Request *toclient);
void ack(int sock, Request *toclient);
void filesender(int sock, char *directorie, char *name, Request *toclient, Request *fromclient);
void filedeleter(int sock, char *directorie, char *name, Request *toclient, Request *fromclient);
void directoriedeleter(int sock, char *directorie, char *name, Request *toclient, Request *fromclient);
void recvfile(int sockfd, char *path, Request *fromclient);
void handlefile(Request *fromclient, Request *toclient, DataSpec *dataTh);
void handlefilepurge(Request *fromclient, Request *toclient, DataSpec *dataTh);
void handledirectoriepurge(Request *fromclient, Request *toclient, DataSpec *dataTh);
void download(Request *fromclient, Request *toclient, DataSpec *dataTh);
void purgeother(Request *fromclient, Request *toclient, DataSpec *dataTh);
void uploaddir(int sock, char *directorie, Request *fromclient, Request *toclient);
void upload(int sock, char *directorie, Request *fromclient, Request *toclient);
void purgeself(int sock, char *directorie, Request *fromclient, Request *toclient);
int checkvisited(char *directorie, int *indice);
void quit(char *directorie);
void handleinit(Request *fromclient, Request *toclient, DataSpec *dataTh, int *indice, int check);
void handlesyncup(Request *fromclient, Request *toclient, DataSpec *dataTh);
void handlesyncdown(Request *fromclient, Request *toclient, DataSpec *dataTh);
void *threadWorker(void *arg);

int main(int argc, char *argv[])
{
  short port;
  int canal, ret;
  struct sockaddr_in adrEcoute, adrClient;
  unsigned int lgAdrClient;
  int numWorkerLibre;

  signal(SIGINT, sig_handler);

  creerCohorteWorkers();

  ret = sem_init(&semWorkersLibres, 0, NB_WORKERS);

  for (int i = 0; i < 1024; i++)
  {

    strcpy(tabvisited[i].directorie, "free");
  }

  if (argc != 2)
    printf("usage: %s port\n", argv[0]);

  port = (short)atoi(argv[1]);

  ecoute = socket(AF_INET, SOCK_STREAM, 0);

  adrEcoute.sin_family = AF_INET;
  adrEcoute.sin_addr.s_addr = INADDR_ANY;
  adrEcoute.sin_port = htons(port);


  bind(ecoute, (struct sockaddr *)&adrEcoute, sizeof(adrEcoute));

  listen(ecoute, 5);

  printf("Le serveur est en fonctionnement...\n");

  while (1)
  {
    
    canal = accept(ecoute, (struct sockaddr *)&adrClient, &lgAdrClient);

    ret = sem_wait(&semWorkersLibres);

    numWorkerLibre = chercherWorkerLibre();

    dataSpec[numWorkerLibre].canal = canal;
    sem_post(&dataSpec[numWorkerLibre].sem);
  }

  close(ecoute);

  exit(EXIT_SUCCESS);
}

void creerCohorteWorkers(void) //creer une cohorte de worker
{
  int i, ret;

  for (i = 0; i < NB_WORKERS; i++)
  {
    dataSpec[i].canal = -1;
    dataSpec[i].tid = i;
    ret = sem_init(&dataSpec[i].sem, 0, 0);

    ret = pthread_create(&dataSpec[i].id, NULL, threadWorker, &dataSpec[i]);
  }
}

// retourne le no. du worker libre trouve ou -1 si pas de worker libre
int chercherWorkerLibre(void)
{
  int numWorkerLibre = -1, i = 0, canal;

  while (numWorkerLibre < 0 && i < NB_WORKERS)
  {
    lockMutexCanal(i);
    canal = dataSpec[i].canal;
    unlockMutexCanal(i);

    if (canal == -1)
      numWorkerLibre = i;
    else
      i++;
  }

  return numWorkerLibre;
}

void lockMutexCanal(int numWorker)
{
  int ret;

  ret = pthread_mutex_lock(&mutexCanal[numWorker]);
}

void unlockMutexCanal(int numWorker)
{
  int ret;

  ret = pthread_mutex_unlock(&mutexCanal[numWorker]);
}

void sig_handler(int sig) // permet de fermer les sockets ouverts lors du signal de fin
{

  for (int i = 0; i < 10; i++)
  {

    close(dataSpec[NB_WORKERS].canal);
  }

  close(ecoute);
  exit(0);
}

void _mkdir(const char *dir) //makedir recusif from www.stackoverflow.com
{
  char tmp[256];
  char *p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", dir);
  len = strlen(tmp);
  if (tmp[len - 1] == '/')
    tmp[len - 1] = 0;
  for (p = tmp + 1; *p; p++)
    if (*p == '/')
    {
      *p = 0;
      mkdir(tmp, S_IRWXU);
      *p = '/';
    }
  mkdir(tmp, S_IRWXU);
}

void ask(int sock, Request *fromclient, Request *toclient) //fonction qui envoie une requete et en envoie une autre
{

  write(sock, (void *)toclient, sizeof(Request));
  if (read(sock, (void *)fromclient, sizeof(Request)) <= 0)
  {
    fprintf(stderr, "No response!\n");
  }
}

void ack(int sock, Request *toclient) // fonction qui envoie une requete sans attendre de réponse
{
  write(sock, (void *)toclient, sizeof(Request));
}

void filesender(int sock, char *directorie, char *name, Request *toclient, Request *fromclient) //gere l'envoie de fichiers en appelant sendfile
{
  char newfile[1024];
  struct stat st;

  strcpy(toclient->action, "file");
  strcpy(toclient->directorie, directorie);
  strcpy(toclient->name, name);
  strcpy(newfile, directorie);
  strcat(newfile, "/");
  strcat(newfile, name);
  stat(newfile, &st);

  toclient->size = st.st_size;
  toclient->time = st.st_ctim;

  ask(sock, fromclient, toclient);
  if (!strcmp(fromclient->action, "ok"))
  {

    ssize_t ret = toclient->size;
    ssize_t lu;

    int fd = open(newfile, O_RDONLY);

    while (ret)
    {
      lu = sendfile(sock, fd, NULL, ret);
      if (lu == -1)
      {
        printf("Something went wrong with sendfile()! %s\n", strerror(errno));
      }
      ret = ret - lu;
    }

    close(fd);
  }
}

void filedeleter(int sock, char *directorie, char *name, Request *toclient, Request *fromclient) //gere la suppression de fichiers en appellant remove
{

  strcpy(toclient->action, "fpurge");
  strcpy(toclient->directorie, directorie);
  strcpy(toclient->name, name);

  char newfile[1024];
  strcpy(newfile, directorie);
  strcat(newfile, "/");
  strcat(newfile, name);

  ask(sock, fromclient, toclient);
  if (!strcmp(fromclient->action, "delete"))
  {
    printf("Suppression du fichier %s\n",newfile);
    remove(newfile);
  }
  else
  {
  }
}

void directoriedeleter(int sock, char *directorie, char *name, Request *toclient, Request *fromclient) //gere la suppression de repertoire en appellant rmdir
{

  strcpy(toclient->action, "dpurge");
  strcpy(toclient->directorie, directorie);
  strcpy(toclient->name, name);

  char newfile[1024];
  strcpy(newfile, directorie);
  strcat(newfile, "/");
  strcat(newfile, name);

  ask(sock, fromclient, toclient);
  if (!strcmp(fromclient->action, "delete"))
  { printf("Suppresion du dossier %s\n",newfile);
    rmdir(newfile);
  }
  else
  {
  }
}

void recvfile(int sockfd, char *path, Request *fromclient) // reception des fichiers
{

  size_t buffersize = 1024 * 16;
  char *buffer[buffersize];
  size_t size = fromclient->size;
  ssize_t lu;
  _mkdir(fromclient->directorie);

  int fd = open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  printf("Création de %s\n",path);

  while (size)
  {
    lu = read(sockfd, buffer, (size > buffersize) ? buffersize : size);
    size = size - lu;
    write(fd, buffer, lu);
  }

  close(fd);
  return;
}

void handlefile(Request *fromclient, Request *toclient, DataSpec *dataTh) // handle les requetes file en vérifiant si le fichier existe
{

  char path[2048];
  DIR *dir = opendir(fromclient->directorie);
  if (dir == NULL)
  { printf("Création du dossier %s\n",fromclient->directorie);
    _mkdir(fromclient->directorie);
  }
  closedir(dir);
  strcpy(path, fromclient->directorie);
  strcat(path, "/");
  strcat(path, fromclient->name);
  struct stat st;
  int ret = stat(path, &st);

  if (!ret && (fromclient->time.tv_sec - st.st_ctim.tv_sec) <= 60) //verifie que la version est la plus récente
  {
    strcpy(toclient->action, "no");
    ack(dataTh->canal, toclient);
  }
  else
  {
    strcpy(toclient->action, "ok");
    ack(dataTh->canal, toclient);
    recvfile(dataTh->canal, path, fromclient);
  }
}

void handlefilepurge(Request *fromclient, Request *toclient, DataSpec *dataTh) // handle le requetes purge de suppression des fichiers sur le client
{

  char path[2048];
  strcpy(path, fromclient->directorie);
  strcat(path, "/");
  strcat(path, fromclient->name);
  struct stat st;
  int ret = stat(path, &st);

  if (!ret)
  {
    strcpy(toclient->action, "ok");
    ack(dataTh->canal, toclient);
  }
  else
  {
    strcpy(toclient->action, "delete");
    ack(dataTh->canal, toclient);
  }
}

void handledirectoriepurge(Request *fromclient, Request *toclient, DataSpec *dataTh) // meme chose pour les dossiers
{

  char path[2048];

  strcpy(path, fromclient->directorie);
  strcat(path, "/");
  strcat(path, fromclient->name);

  DIR *dir = opendir(path);
  if (dir == NULL)
  {

    strcpy(toclient->action, "delete");
    ack(dataTh->canal, toclient);
  }

  else
  {

    strcpy(toclient->action, "ok");
    ack(dataTh->canal, toclient);
  }
  closedir(dir);
}

void download(Request *fromclient, Request *toclient, DataSpec *dataTh) //gere le téléchargement des fichiers depuis les clients
{

  while (1)
  {
    read(dataTh->canal, (void *)fromclient, sizeof(Request));

    if (strcmp(fromclient->action, "file") == 0)
    {
      handlefile(fromclient, toclient, dataTh);
    }
    else if (strcmp(fromclient->action, "dir") == 0)
    {
      _mkdir(fromclient->directorie);
      strcpy(toclient->action, "ok");
      ack(dataTh->canal, toclient);
    }
    else if (strcmp(fromclient->action, "done") == 0)
    {
      break;
    }
  }
}

void purgeother(Request *fromclient, Request *toclient, DataSpec *dataTh) //permet de gerer les requetes de suppression de fichiers sur le client
{

  while (1)
  {
    read(dataTh->canal, (void *)fromclient, sizeof(Request));

    if (strcmp(fromclient->action, "fpurge") == 0)
    {
      handlefilepurge(fromclient, toclient, dataTh);
    }
    else if (strcmp(fromclient->action, "dpurge") == 0)
    {

      handledirectoriepurge(fromclient, toclient, dataTh);
    }
    else if (strcmp(fromclient->action, "done") == 0)
    {
      break;
    }
  }
}

void uploaddir(int sock, char *directorie, Request *fromclient, Request *toclient) //permet d'upload au client un dossier
{

  strcpy(toclient->action, "dir");
  strcpy(toclient->directorie, directorie);
  ask(sock, fromclient, toclient);
}

void upload(int sock, char *directorie, Request *fromclient, Request *toclient) // gere l'upload sur un client
{

  DIR *dir = opendir(directorie);

  if (dir == NULL)
  {

    return;
  }

  struct dirent *ent;

  while ((ent = readdir(dir)) != NULL)
  {

    if (ent->d_name[0] == '.')
    {
    }

    else
    {

      if (ent->d_type == 8)

      {

        filesender(sock, directorie, ent->d_name, toclient, fromclient);
      }
      else if (ent->d_type == 4)
      {

        char newdirectorie[1024];
        strcpy(newdirectorie, directorie);
        strcat(newdirectorie, "/");
        strcat(newdirectorie, ent->d_name);
        upload(sock, newdirectorie, fromclient, toclient);
        uploaddir(sock, newdirectorie, fromclient, toclient);
      }
    }
  }

  closedir(dir);
}

void purgeself(int sock, char *directorie, Request *fromclient, Request *toclient) //gere la suppression de fichier sur le serveur lui meme
{

  DIR *dir = opendir(directorie);

  if (dir == NULL)

    return;

  struct dirent *ent;

  while ((ent = readdir(dir)) != NULL)
  {

    if (ent->d_name[0] == '.')
    {
    }

    else
    {

      if (ent->d_type == 8)

      {

        filedeleter(sock, directorie, ent->d_name, toclient, fromclient);
      }
      else if (ent->d_type == 4)
      {

        char newdirectorie[1024];
        strcpy(newdirectorie, directorie);
        strcat(newdirectorie, "/");
        strcat(newdirectorie, ent->d_name);
        purgeself(sock, newdirectorie, fromclient, toclient);
        directoriedeleter(sock, directorie, ent->d_name, toclient, fromclient);
      }
    }
  }

  closedir(dir);
}

int checkvisited(char *directorie, int *indice) //permet de connaitre si des threads sont sur le meme dossier en lecture ou écriture
{
  int k = 0;

  for (int i = 0; i < 1024; i++)
  {

    if (strcmp(tabvisited[i].directorie, directorie) == 0)
    {
      if (tabvisited[i].bool)
      {

        *indice = i;

        return 1;
      }
      else
      {

        tabvisited[i].bool = 1;
        *indice = i;
        return 0;
      }
    }
  }

  while (strcmp(tabvisited[k].directorie, "free"))
  {
    k++;
  }

  strcpy(tabvisited[k].directorie, directorie);
  tabvisited[k].mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  tabvisited[k].bool = 1;
  *indice = k;
  return 0;
}

void quit(char *directorie) //permet à un thread de signaler qu'il a terminé
{
  int k;

  while (strcmp(tabvisited[k].directorie, directorie))
  {
    k++;
  }
  tabvisited[k].bool = 0;
}

void handleinit(Request *fromclient, Request *toclient, DataSpec *dataTh, int *indice, int check) // gere l'initialisation
{
  DIR *dir = opendir(fromclient->directorie);

  int mode = 0;
  if (!strcmp(fromclient->name, "write"))
  { 
    printf("Un nouveau client demande la connexion en mode écriture pour le dossier: %s\n",fromclient->directorie);
    mode = 1;
  }
  else{
    printf("Un nouveau client demande la connexion en mode lecture pour le dossier: %s\n",fromclient->directorie);
  }

  if (dir == NULL)
  {printf("Le dossier n'existe pas, il est donc crée\n");
    _mkdir(fromclient->directorie);

    if (mode)
    { printf("Nouveau client connecté en mode écriture\n");
      strcpy(toclient->action, "writed");
      ack(dataTh->canal, toclient);
      download(fromclient, toclient, dataTh);
    }
    else
    { printf("Nouveau client connecté en mode lecture\n");
      strcpy(toclient->action, "readvoid");
      ack(dataTh->canal, toclient);
    }
  }
  else
  { printf("Le dossier existe\n");
    closedir(dir);

    if (!check && mode)
    {
      printf("Nouveau client connecté en mode écriture\n");
      strcpy(toclient->action, "writeu");
      ack(dataTh->canal, toclient);
    }
    else
    { printf("Nouveau client connecté en mode lecture\n");
      strcpy(toclient->action, "read");
      ack(dataTh->canal, toclient);
    }
    upload(dataTh->canal, fromclient->directorie, fromclient, toclient);
    strcpy(toclient->action, "done");
    ack(dataTh->canal, toclient);
  }

  purgeother(fromclient, toclient, dataTh);
}

void handlesyncup(Request *fromclient, Request *toclient, DataSpec *dataTh) // synchronisation du serveur avec le client
{

  _mkdir(fromclient->directorie);
  strcpy(toclient->action, "ok");
  ack(dataTh->canal, toclient);
  download(fromclient, toclient, dataTh);
}

void handlesyncdown(Request *fromclient, Request *toclient, DataSpec *dataTh) // synchronisation du client avec le serveur
{

  strcpy(toclient->action, "ok");
  ack(dataTh->canal, toclient);
  upload(dataTh->canal, fromclient->directorie, fromclient, toclient);
  strcpy(toclient->action, "done");
  ack(dataTh->canal, toclient);
}

void *threadWorker(void *arg) // fonction d'un thread
{
  DataSpec *dataTh = (DataSpec *)arg;

  Request fromclient;
  Request toclient;
  char workingdirectorie[1024];
  int indice;
  int ret;
  while (1)
  {
    ret = sem_wait(&dataTh->sem);

    read(dataTh->canal, (void *)&fromclient, sizeof(Request));


    pthread_mutex_lock(&lock);
    int check = checkvisited(fromclient.directorie, &indice);
    pthread_mutex_unlock(&lock);

    if (strcmp(fromclient.action, "init") == 0) //gestion demande d'initialisation
    {

      strcpy(workingdirectorie, fromclient.directorie);
      handleinit(&fromclient, &toclient, dataTh, &indice, check);
    }

    while (read(dataTh->canal, (void *)&fromclient, sizeof(Request))) // gestion des requetes
    {
      pthread_mutex_lock(&(tabvisited[indice].mutex));

      if (strcmp(fromclient.action, "purge") == 0)
      {

        strcpy(toclient.action, "ok");
        ack(dataTh->canal, &toclient);
        purgeself(dataTh->canal, fromclient.directorie, &fromclient, &toclient);
        strcpy(toclient.action, "done");
        ack(dataTh->canal, &toclient);
      }
      else if (strcmp(fromclient.action, "syncup") == 0)
      {
        handlesyncup(&fromclient, &toclient, dataTh);
      }

      else if (strcmp(fromclient.action, "syncdown") == 0)
      {

        handlesyncdown(&fromclient, &toclient, dataTh);
      }
      else if (strcmp(fromclient.action, "purgeself") == 0)
      {

        strcpy(toclient.action, "ok");
        ack(dataTh->canal, &toclient);
        purgeother(&fromclient, &toclient, dataTh);
      }
      pthread_mutex_unlock(&(tabvisited[indice].mutex));
    }
    pthread_mutex_lock(&lock);
    quit(workingdirectorie); //signale aux autres thread la fin de tache
    pthread_mutex_unlock(&lock);

    lockMutexCanal(dataTh->tid);
    dataTh->canal = -1;
    unlockMutexCanal(dataTh->tid);

    printf("Client du dossier %s déconnecté\n",workingdirectorie);

    ret = sem_post(&semWorkersLibres);
  }

  pthread_exit(NULL);
}
