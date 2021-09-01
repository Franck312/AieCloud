//Les fonctions homonymes à celles du serveur ne sont pas commentées, se reporter à serveur.c

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
#include <string.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <errno.h>

#define CMD "client"
#define MAX_EVENTS 1024                           
#define LEN_NAME 32 //16                                                       
#define EVENT_SIZE (sizeof(struct inotify_event)) 
#define BUF_LEN (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))
#define WAITTIME 10 // durée avant la synchronisation lorsque un changement est détecté(On attend la fin de l'écriture)
#define SYNCTIME 5 // Période de synchronisation en mode read


typedef struct // Structure d'une requete
{
    char action[10];
    char directorie[1024];
    char name[1024];
    off_t size;
    struct timespec time;
} Request; 


int fd, wd;
int sock;
int writing;
Request toserver;
Request fromserver;




void _mkdir(const char *dir) { //mkdir recursif from www.stackoverflow.com
        char tmp[256];
        char *p = NULL;
        size_t len;

        snprintf(tmp, sizeof(tmp),"%s",dir);
        len = strlen(tmp);
        if(tmp[len - 1] == '/')
                tmp[len - 1] = 0;
        for(p = tmp + 1; *p; p++)
                if(*p == '/') {
                        *p = 0;
                        mkdir(tmp, S_IRWXU);
                        *p = '/';
                }
        mkdir(tmp, S_IRWXU);
}


void ask() 
{
    write(sock, (void *)&toserver, sizeof(Request));
    if (read(sock, (void *)&fromserver, sizeof(Request)) <= 0)
    {
        fprintf(stderr, "Connexion avec le serveur interrompu\n");
        exit(0);
    }
}

void ack()
{
    
    write(sock, (void *)&toserver, sizeof(Request));
    
}

void recvfile(char *directorie)
{
    size_t buffersize= 1024*16;
    char *buffer[buffersize];
    size_t size = fromserver.size;
    ssize_t lu;

    _mkdir(fromserver.directorie);

    int fd = open(directorie,O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);

    printf("Création de %s",directorie);
    
    
    while (size) { 
        lu=read(sock,buffer,(size>buffersize)?buffersize: size);
        size= size-lu;
        write(fd,buffer,lu);
    }


    close(fd);
    return;
}

void filesender(char *directorie, char *name)
{
    strcpy(toserver.action, "file");
    strcpy(toserver.directorie, directorie);
    strcpy(toserver.name, name);

     char newfile[1024];
    strcpy(newfile, directorie);
    strcat(newfile, "/");
    strcat(newfile, name);
    struct stat st;
    stat(newfile, &st);
     toserver.size = st.st_size;
     toserver.time = st.st_ctim;


    ask();
    if (!strcmp(fromserver.action, "ok"))
    {

        
        ssize_t ret= toserver.size;
        ssize_t lu;


        int fd= open(newfile, O_RDONLY);
        
        while(ret){
            lu= sendfile(sock, fd, NULL, ret);
            if (lu==-1){
                fprintf(stderr,"Something went wrong with sendfile()! %s\n", strerror(errno));
            }
            ret= ret - lu;
        }


        close(fd);
    }
    else
    {
    }
}

void filedeleter(char* directorie,char* name)
{
    
    strcpy(toserver.action, "fpurge");
    strcpy(toserver.directorie, directorie);
    strcpy(toserver.name, name);


    char newfile[1024];
    strcpy(newfile, directorie);
    strcat(newfile, "/");
    strcat(newfile, name);

    ask();
    if (!strcmp(fromserver.action, "delete"))
    {
        printf("Suppresion du fichier %s\n",newfile);
      remove(newfile);

    
    }
}


void directoriedeleter(char* directorie,char* name)
{
    
    strcpy(toserver.action, "dpurge");
    strcpy(toserver.directorie, directorie);
    strcpy(toserver.name, name);


    char newfile[1024];
    strcpy(newfile, directorie);
    strcat(newfile, "/");
    strcat(newfile, name);

    ask();
    if (!strcmp(fromserver.action, "delete"))
    {
      printf("Suppresion du dossier %s\n",newfile);
      rmdir(newfile);

    
    }
    else
    {
    }
}

void handlefile()
{
    
    char path[2048];
    DIR* dir= opendir(fromserver.directorie);
    if (dir == NULL)
    {    printf("Création du dossier %s\n",fromserver.directorie);
        _mkdir(fromserver.directorie);
        
    }
    closedir(dir);
    
    strcpy(path, fromserver.directorie);
    strcat(path, "/");
    strcat(path, fromserver.name);
    struct stat st;
    int ret = stat(path, &st);

    
    
    if (!ret && (fromserver.time.tv_sec-st.st_ctim.tv_sec)<0)
    {
        
        strcpy(toserver.action, "no");
        ack();
    }
    else
    {
        
        strcpy(toserver.action, "ok");
        ack();
        recvfile(path);
    }

    
}


void handlefilepurge(){

  
    char path[2048];

    
    strcpy(path, fromserver.directorie);
    strcat(path, "/");
    strcat(path, fromserver.name);
    struct stat st;
    int ret = stat(path, &st);

    
    
    if (!ret)
    {
        
        strcpy(toserver.action, "ok");
        ack();
    }
    else
    {
        
        strcpy(toserver.action, "delete");
        ack();
    }


}

void handledirectoriepurge(){

  
    char path[2048];

    


  strcpy(path, fromserver.directorie);
    strcat(path, "/");
    strcat(path, fromserver.name);

DIR* dir= opendir(path);
    if (dir == NULL)
    {
        
        strcpy(toserver.action, "delete");
        ack();

    }
    
    else
    {
        
        strcpy(toserver.action, "ok");
        ack();
    }

closedir(dir);
}


void download()
{

    
    while (1)
    {
        
        read(sock, (void *)&fromserver, sizeof(Request));

        if (strcmp(fromserver.action, "file") == 0)
        {
            handlefile();
        }
        else if (strcmp(fromserver.action, "dir") == 0)
        {
            _mkdir(fromserver.directorie);
            strcpy(toserver.action,"ok");
            ack();
            
        }
        else if (strcmp(fromserver.action, "done") == 0)
        {
            break;
        }
    }
    
}


void purgeother(){


    while(1){
    read(sock,(void*) &fromserver,sizeof(Request));

  if(strcmp(fromserver.action,"fpurge")==0){
  handlefilepurge();
    
    
  }
  else if(strcmp(fromserver.action,"dpurge")==0){
      
  handledirectoriepurge();
    
  }
  else if (strcmp(fromserver.action,"done")==0){
      
      break;
  }
  
  }


}

void uploaddir(char *directorie){

strcpy(toserver.action,"dir");
strcpy(toserver.directorie,directorie);
ask();
}

void upload(char *directorie)
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
                filesender(directorie, ent->d_name);
            }
            else if (ent->d_type == 4)
            {
                char newdirectorie[1024];
                strcpy(newdirectorie, directorie);
                strcat(newdirectorie, "/");
                strcat(newdirectorie, ent->d_name);
                upload(newdirectorie);
                uploaddir(newdirectorie);
                
            }
           
        }
    }

    closedir(dir);
}


void purgeself(char *directorie)
{
  
    DIR *dir = opendir(directorie);


    if (dir == NULL)
        return ;

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
                filedeleter(directorie,ent->d_name);
            }
            else if (ent->d_type == 4)
            {   
                char newdirectorie[1024];
                strcpy(newdirectorie,directorie);
                strcat(newdirectorie, "/");
                strcat(newdirectorie, ent->d_name);
                purgeself(newdirectorie);
                directoriedeleter(directorie,ent->d_name); //use new direc
            }
            else
            {
            }
        }

    }

    closedir(dir);
}

void initco(char *directorie,int mode) //initialisation de la connexion
{

    strcpy(toserver.action, "init");
    
    strcpy(toserver.directorie, directorie);
    
    if(mode){strcpy(toserver.name, "write");
    }
    else {
        strcpy(toserver.name, "read");
    }
    ask();

    if (!strcmp(fromserver.action, "writed"))

    {   printf("\n---------------------- mode écriture -------------------------------\n");
        writing=1;
        upload(directorie);
        strcpy(toserver.action, "done");
        ack();

    }
    else if (!strcmp(fromserver.action, "writeu"))

    {   printf("\n---------------------- mode écriture -------------------------------\n");
        writing=1;
        download();
    }
    else if (!strcmp(fromserver.action, "read"))
    {   printf("\n---------------------- mode lecture -------------------------------\n");
        writing=0;
        download();
    }
    else if (!strcmp(fromserver.action, "readvoid"))
    {
        printf("\n---------------------- mode lecture -------------------------------\n");
        writing=0;
    }
    purgeself(directorie);
    strcpy(toserver.action,"done");
    ack();
}

void syncup(char *directorie) //synchronisation en mode upload
{
    
    strcpy(toserver.action, "syncup");
    stpcpy(toserver.directorie, directorie);
    ask();
    
    if (!strcmp(fromserver.action, "ok"))
    {
        upload(directorie);
    }

    
    strcpy(toserver.action,"done");
    ack();

}

void syncdown(char *directorie) //synchonisation en mode download
{

    strcpy(toserver.action, "syncdown");
    strcpy(toserver.directorie,directorie);
    ask();
    if (!strcmp(fromserver.action, "ok"))
    {

        download();
    }

}

void addwatch(char *directorie,int fd,char** directoriewatched) // ajout de watcher pour chaque directorie de manière récursive
{   int a;
    
    a =inotify_add_watch(fd, directorie, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO );
    strcpy(directoriewatched[a],directorie);
    
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
            if (ent->d_type == 4)
            {
                char newdirectorie[1024];
                strcpy(newdirectorie, directorie);
                strcat(newdirectorie, "/");
                strcat(newdirectorie, ent->d_name);
                addwatch(newdirectorie,fd,directoriewatched);
            }

        }
    }

    closedir(dir);
}



int main(int argc, char *argv[])
{
    int ret;
    struct sockaddr_in adrServ;
    int mode=0;
    char* directoriewatched[1024];
    struct in_addr server_addr;

    for(int i=0;i<1024;i++){
        directoriewatched[i]=malloc(sizeof(char)*1024);
    }

    if (argc != 5)
        printf("usage: %s machine port directorie\n", argv[0]);


    if(!strcmp(argv[4],"write")){
        
        mode=1;
    }
    printf("%s: creating a socket\n", CMD);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    
    
    adrServ.sin_family = AF_INET;
    
    inet_aton(argv[1], &server_addr);
    
    memcpy(&(adrServ.sin_addr), &server_addr, sizeof(server_addr));
    
    adrServ.sin_port = htons(atoi(argv[2]));

    printf("%s: connecting the socket\n", CMD);

    ret = connect(sock, (struct sockaddr *)&adrServ, sizeof(struct sockaddr_in));

    if (ret < 0)
        fprintf(stderr,"Something went wrong with connect()! %s\n", strerror(errno));

    
    char * directorie = argv[3];
    _mkdir(directorie);
    initco(directorie,mode);


if(writing){

printf("Connecté en mode écriture\n");

fd = inotify_init();
addwatch(directorie,fd,directoriewatched);
    while (1)
    {
        struct inotify_event old;
        int add;
        int i = 0, length;
        char buffer[BUF_LEN];
        length = read(fd, buffer, BUF_LEN); // lecture des nouveaux événements
	printf("Nouvel événement détecté\n");
        sleep(WAITTIME);// On s'assure que la copie du fichier soit terminée


        
        while (i < length)
        {

            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len )
            {
            //test si l'événement est correct
            int typeevent=((event->mask & IN_CREATE) || (event->mask & IN_MODIFY) || event->mask & IN_MOVED_TO);
            int typeold=((old.mask & IN_CREATE) || (old.mask & IN_MODIFY) || old.mask & IN_MOVED_TO);
            
            if( !(strcmp(old.name,event->name)==0) || !(typeevent == typeold) ){ // réaction en fonction de la nature de l'événement
                printf("Synchronisation du serveur\n");

                if(typeevent){
                    syncup(directorie);
                    

                }
                else{
                
                    strcpy(toserver.action,"purge");
                    strcpy(toserver.directorie,directorie);
                    ask();
                    purgeother();

                }
                close(fd);

                fd= inotify_init(); //réarmement du la surveillance des dossiers
                addwatch(directorie,fd,directoriewatched); 
                

            }
            }
            i += EVENT_SIZE + event->len;
        }
        
    }
}

else
{   printf("Connecté en mode lecture\n");
    while(1){
        sleep(SYNCTIME);
        printf("Synchronisation\n");
        syncdown(directorie);
        strcpy(toserver.action,"purgeself");
        ask();
        purgeself(directorie);
        strcpy(toserver.action,"done");
        ack();
    }
}


    close(sock);
        

    exit(EXIT_SUCCESS);
}
