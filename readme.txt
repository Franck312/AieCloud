Disclaimer:
Ce programme nécessite d'être attentif aux chemins de fichiers utilisés lorsqu'un serveur et
plusieurs clients sont exécutés sur la même machine.
L’archive qui vous est fournie contient une arborescence de fichiers permettant de tester
directement ce programme en exécutant un serveur et des clients sur la même machine.
Il est possible d’utiliser ce programme hors de cette arborescence mais il faut alors faire
attention à ce qu’aucune instance client ou serveur ne s'exécute dans le même dossier.

1) Compilation

Se placer dans AieCloud , puis exécuter les commandes:
$make serveur
$make client

2) Exécution du serveur

Toujours dans AieCloud, exécuter la commande:
$./serveur [PORT]
ex: $./serveur 2000
Pour lancer le serveur sur le port 2000.

3) Exécution des clients

Se placer cette fois ci dans AieCloud/Client1 puis exécuter la commande:
$../client [IP du serveur] [PORT] [chemin relatif du dossier synchronisé] [mode write ou read]
ex: $../client 127.0.0.1 2000 test write
demande la synchronisation du dossier test situé dans Client1 en mode écriture avec le
serveur lancé sur la même machine (l’abréviation localhost n’est pas prise en charge).
Le dossier à synchroniser n’a pas besoin d’exister, il sera créé si nécessaire.
Pour exécuter un deuxième client recommencer en se plaçant cette fois dans XXX/Client2.
Pour exécuter un n-ième client, il faut créer un nouveau dossier (ex: ClientN) et répéter la
manoeuvre.

4) Utilisation

Il n’y a aucune input a fournir au serveur ou aux clients une fois ceux-ci lancés.
Peu importe le mode sélectionné, si le dossier donné en argument existe sur le serveur
celui-ci écrasera le contenu local du client lors de la connexion. Si le dossier n’est pas connu
du serveur alors, si le client est en mode write le contenu du dossier client est copié sur le
serveur si il est en mode read le dossier client est vidé.
Si le client est en mode write, toute modification du dossier synchronisé entraînera une
modification sur le serveur et donc chez les autres clients en mode read sur le même
dossier.

Si le client est en mode read, il se synchronise de manière périodique avec le serveur. Si
vous tentez d’ajouter un fichier dans le dossier il sera supprimé à la prochaine
synchronisation car il n’est pas présent sur le serveur.
Un seul client peut être en mode write sur le même dossier pour éviter tout conflit. Si un
client tente de se connecter en mode write alors qu’un autre client est déjà connecté dans ce
mode, il sera automatiquement connecté en mode lecture.
Le serveur stocke les fichiers dans AieCloud/Nomdudossiersynchronise/ si le dossier
n’existe pas il sera créé lors de la connexion du client.

5) Fin de tâche

Pour terminer le programme, serveur ou client, il suffit de saisir dans le terminal ctrl+c, la
connexion sera terminée proprement.