#include <stdio.h> 
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>

//pour stat serveur 
int total_recus = 0;
int total_valides = 0;
int total_corrompus = 0;

struct Message  //composition des message
{
	int taille; //taille données 
	short  nb_port_desti; //numéro port destination
	short   nb_mess;   //numéro message
	unsigned int  *donnee; // donnée partagé
	short   checksum;
};
typedef struct Message message;

//structure de file de message et des données de serveurs 
struct file_msg_fragment
{
    long type;              // type pour msgrcv 
    short num_mess;         // numéro du message complet
    short fragment_id;      // numéro du fragment
    short nb_fragments;     // nombre total de fragments
    short taille_fragment;
    unsigned int taille_totale;  // taille du fragment (en entiers)
    short checksum;         // checksum 
    unsigned int donnee[1000]; // données du fragment
};
typedef struct file_msg_fragment file_m;

struct message_reconstruit  // strucuture pour reconstruire le message 
{
    unsigned int *donnee;
    int nb_fragments;
    int fragments_recus;
    int taille_totale;
    int num_mess;
    unsigned short checksum;
};
typedef struct message_reconstruit msg_recons;

unsigned short  calcul_checksum(unsigned int *data ,int d,unsigned int somme) //fonction du calcul de checksum de base pour pas recopier plusieurs fois la même fonction
{

	for (int s = 0;s<d;s++)
	{
		unsigned short  moitie1 = (data[s] >>16) & 0xFFFF; //premier moitie des 32 bits d'une donnée car on met sur 16 bits
		unsigned short  moitie2 = data[s] & 0xFFFF; // la deuxieme moitie pour une donnée car on met sur 16 bits
		
		somme += moitie1;   //ajoute a la somme la premiere moitié des 32 bits
		somme = (somme & 0xFFFF) + (somme >> 16);  // gère la retenu en prenant les 16 derniers bits et en ajoutant ce qui dépasse

		somme += moitie2; // ajoute a la somme la deuxième moitié des 32 bits
		somme = (somme & 0xFFFF) + (somme >> 16); //gère la retenu en prenant les 16 derniers bits et en ajoutant ce qui dépasse
	}
	return (unsigned short)(somme & 0xFFFF);
	
}	

unsigned short checksum_client_transport(message *mes,int d ) //fonction pour calculer le checksum client 
{
 unsigned int somme = mes->nb_port_desti +  mes->nb_mess+mes->taille; //ajoute a la somme l'entête client 
 return calcul_checksum(mes->donnee,d,somme);
}

unsigned short checksum_transport_serveur(unsigned int *donnee,int taille,int num_mess) // fonction pour calculer le checskum sans l'entête client 
{
    unsigned int somme = taille + num_mess; // ajoute a somme l'entête crée par transport pour serveur 
    return calcul_checksum(donnee, taille, somme);
}

void generation_message(message *mes, int d, int port_dest, int num_mess) // fonction pour la génération des messages 
{
    mes->nb_port_desti = port_dest;
    mes->nb_mess = num_mess;
	mes->taille = d;
    for (int k = 0; k < d; k++)
    {
        mes->donnee[k] = (unsigned int) rand();
    }
}
void envoyer_message_fragments(int tube_mess_transp, unsigned int *donnee, int taille)
{
    int taille_fragment = 10000;
    int debut = 0;
    int nb = 0;
    while(debut < taille)
    {
        int count; //nb d'entier par fragment
        if (debut+taille_fragment > taille)
        {
            count = taille - debut; // si il reste moins d'entier que taille_fragment il prend ce qu'il reste pour l'ajouter dans le write 
        }
        else 
        {
            count = taille_fragment;
        }
        int nb_octets = count* sizeof(unsigned int); // on met en octets 
        int ecrit = 0;  // pour voir le nb d'octets deja écris 
        while (ecrit < nb_octets)
        {
            int val = write(tube_mess_transp,((char*)donnee) + debut*sizeof(unsigned int) + ecrit, nb_octets - ecrit ); //ecris une partie du fragment dans le pipe et renvoie le nombre d'octets écris 
            if (val <= 0) 
            {
                perror("write");
                exit(1);
            } 
            ecrit+=val; // ajoute le nombre d'octets déjà écris 
        }
        nb++;
        if (nb%50==0)
        {
            printf("[ATT] fragment numero %d envoyer\n",nb);
            fflush(stdout);
        }
        
        debut +=count ;
        
    }
}

void client (int i,int n,int d,int tube_verif[][2],int tube_mess_transp[2],int semid_client)
{
    
    struct sembuf P;
    P.sem_num = 0;
    P.sem_op  = -1;
    P.sem_flg = 0;

    struct sembuf V;
    V.sem_num = 0;
    V.sem_op  = 1;
    V.sem_flg = 0;
    int total_entiers_envoyes=0;
	int total_messages_envoyes = 0;
    int message_echoués=0;
    for (int k = 0; k < n; k++)
	{
    	if (k != i)  //le client i ne garde que son propre tube donc ferme ceux des autres clients
    	{
    		close(tube_verif[k][0]);
    		close(tube_verif[k][1]);
        }
    }
    close(tube_mess_transp[0]); //ferme le tube en mode lecture  car le client écris vers transport
    close(tube_verif[i][1]);//ferme le tube en mode écriture car le client reçoit la réponse du transport donc il n'écrit pas
				
    srand(123456 + i);// graine de generation 
			
    for (int j = 0; j< 10;j++)  //boucle pour les messages
    {
								
        message mes; // variable de type message 
        mes.donnee = malloc(d * sizeof(unsigned int));
		generation_message(&mes, d,i,j);//génère les messages avec la fonction 		
        unsigned short checksum_client = checksum_client_transport(&mes,d);
		mes.checksum= ~checksum_client; //calcul du checksum
        
        printf("CLient %d envoie message %d\n",i,j); // pareil teste du programme 
        int verifi;
        int tentative_port =0; // variable pour la verification 
        do        // verification du checksum par transport et envoie de 0 ou 1, renvoie le message tant que 1
        {
            semop(semid_client,&P,1);
            write(tube_mess_transp[1], &mes.taille, sizeof(int));
			write(tube_mess_transp[1], &mes.nb_port_desti, sizeof(short));
			write(tube_mess_transp[1], &mes.nb_mess, sizeof(short));
            envoyer_message_fragments(tube_mess_transp[1], mes.donnee,mes.taille);
            
			write(tube_mess_transp[1], &mes.checksum, sizeof(short));
            semop(semid_client,&V,1);
            total_messages_envoyes++;
			total_entiers_envoyes += mes.taille; // pour les statistiques		
            read(tube_verif[i][0],&verifi,sizeof(int)); // lit le message du transport : 1 ou 0
            
            if (verifi ==1) // si 1 alors le calcul du checksum du transport est differents de celui du client 
            {
                exit(0);  //attente de 0 à 5 secondes
            }
            else if (verifi ==2)  // test si port sur écoute par un serveur 
            {
                tentative_port++;
                message_echoués++;
                printf("Aucun serveur. Tentative %d/5\n", tentative_port);
            }
			
			
            if (tentative_port >= 5)
            {
                printf("Port indisponible après 5 tentatives\n");
                // affichage des stats
	            printf("\n=== STATISTIQUES CLIENT  ===\n");
                fflush(stdout);
	            printf("Client %d , Messages envoyés : %d\n",i, total_messages_envoyes);
                fflush(stdout);
	            printf("Entiers envoyés : %d\n", total_entiers_envoyes * d);
                fflush(stdout);
                printf("CLient %d , Message échoués : %d\n",i,message_echoués);
                fflush(stdout);
                exit(1);
            }
					
			sleep((rand() % 3) + 1);  // 1 à 3 secondes
		}
        while (verifi !=0);
		sleep(rand()%6);	//attente de 0 a 5 seconde
        free(mes.donnee); 
	}
	close(tube_verif[i][0]);//fermeture des tubes quand le client a fini
	close(tube_mess_transp[1]);// pareil 
	// affichage des stats
	printf("\n=== STATISTIQUES CLIENT  ===\n");
	printf("Client %d , Messages envoyés : %d\n",i, total_messages_envoyes);
    fflush(stdout);
	printf("Client %d , Entiers envoyés : %d\n",i, total_messages_envoyes );
    fflush(stdout);
    printf("CLient %d , Message échoués : %d\n",i,message_echoués);
    fflush(stdout);
	exit(0);	//exit 
}

void fermer_serveur(int msgid,short *port, int n)
{
   
    file_m fin = {0}; //pour initialiser à 0 toute la structure 
    for (int i = 0; i < n; i++)
    {
        if (port[i] != -1)   // si un serveur écoute sur ce port
        {
            fin.type = i + 1;   // type = numéro du port +1
            fin.nb_fragments = -1;
            msgsnd(msgid, &fin, sizeof(file_m) - sizeof(long), 0);
        }
	}
}
int verif_checksum(message *m)
{
    unsigned int calc = checksum_client_transport(m, m->taille);

    unsigned int total = calc + (unsigned short)m->checksum;

   	while (total >> 16)
	{
    total = (total & 0xFFFF) + (total >> 16);
	}
	printf("le total (transport) est %u\n",total);
    return (total == 0xFFFF) ? 0 : 1;
}

void lire_message_fragments(int tube_mess_transp, unsigned int *donnee, int taille)
{
    int taille_fragment = 10000;
    int debut = 0;
    int nb = 0;
    while(debut < taille)
    {
        int count; //nb d'entier par fragment
        if (debut+taille_fragment > taille)
        {
            count = taille - debut; // si il reste moins d'entier que taille_fragment il prend ce qu'il reste pour l'ajouter dans le write 
        }
        else 
        {
            count = taille_fragment;  //prend un fragment normal 
        }
        int nb_octets = count* sizeof(unsigned int); // on met en octets 
        int lus = 0;  // pour voir le nb d'octets deja écris 
        while (lus < nb_octets)
        {
            int val = read(tube_mess_transp,((char*)donnee + debut*sizeof(unsigned int)) + lus, nb_octets - lus ); //ecris une partie du fragment dans le pipe et renvoie le nombre d'octets écris 
            
            if (val <= 0) 
            {
                perror("read");
                exit(1);
            } 
            lus+=val; // ajoute le nombre d'octets déjà écris 
        }
        nb++;
        if (nb%50==0)
        {
            printf("[WAI] fragment numero %d lus\n",nb);
            fflush(stdout);
        }
        debut +=count ;
    }
}

void envoyer_message_fragments_dans_file(int msgid, unsigned int *donnee, int taille,int port, int num_mess,unsigned short calc)
{
    
    int taille_fragment = 1000;
    int debut = 0;
    int nb = 0;
    int nb_fragment = 0;
    nb_fragment = taille / taille_fragment;
    if (taille % taille_fragment != 0)
        {
            nb_fragment += 1; // prendre en compte le dernier fragment
        }
    while(debut < taille)
    {
        file_m fm;
        int count; //nb d'entier par fragment
        if (debut+taille_fragment > taille)
        {
            count = taille - debut; // si il reste moins d'entier que taille_fragment il prend ce qu'il reste pour l'ajouter dans le write 
        }
        else 
        {
            count = taille_fragment;
        }
        fm.type = port + 1;
        fm.num_mess = num_mess;
        fm.nb_fragments = nb_fragment;
        fm.fragment_id = nb;
        
        fm.taille_fragment = count;
        fm.taille_totale = taille;
        fm.checksum = calc; 
        // Copier les données dans la structure avec une boucle
        for (int i = 0; i < count; i++)
        {
            fm.donnee[i] = donnee[debut + i];
        }
            int val = msgsnd(msgid,&fm,sizeof(file_m) - sizeof(long),0 ); //ecris une partie du fragment dans la file 
            if (val == -1) 
            {
                perror("msgsnd");
                exit(1);
            } 
            
         
        nb++;
        if (nb==50)
        {
            printf("[ATT] fragment numero %d envoyer\n",nb);
            fflush(stdout);
        }
        
        debut +=count ;
    }
        
    
}
void transport (int n,int m,int tube_mess_transp[2],short port[],int tube_verif[][2])

{
	key_t cle_file = ftok("cle_serv",1); 
	if (cle_file == -1) 
    {
        perror("ftok");
        exit(1); 
    }
	int msgid = msgget(cle_file,0666);
	if (msgid ==-1)
	{
		perror("msgget");
		exit(1);
	}
	srand(time(NULL)^ getpid()); // pour avoir une graine de génération par fils 
	
	
	close(tube_mess_transp[1]); // fermeture de l'ecriture 
	for (int i = 0;i<n;i++)
	{
		close(tube_verif[i][0]); //ferme tube de lecture de la verif de tous les clients 
	}
	int verif;
	message m_lue;
    m_lue.donnee = NULL;
	while (read(tube_mess_transp[0],&m_lue.taille,sizeof(int)) > 0)
	{
        m_lue.donnee = malloc(m_lue.taille * sizeof(unsigned int));
    	read(tube_mess_transp[0], &m_lue.nb_port_desti, sizeof(short));
    	read(tube_mess_transp[0], &m_lue.nb_mess, sizeof(short));
    	lire_message_fragments( tube_mess_transp[0], m_lue.donnee, m_lue.taille);
    	read(tube_mess_transp[0], &m_lue.checksum, sizeof(short));

    // traitement normal ici
	
		short serveur_id = port[m_lue.nb_port_desti];
		if (serveur_id == -1)
		{
			verif= 2; // port n'est pas écouté par un serveur
			
		}
		else 
		{
			
			verif = verif_checksum(&m_lue); 
			if (verif==0)
			{
                unsigned short calc = ~checksum_transport_serveur(m_lue.donnee, m_lue.taille,m_lue.nb_mess); // calcul du checksum avec ensuite complement a 1 pour 
				envoyer_message_fragments_dans_file(msgid, m_lue.donnee,m_lue.taille, m_lue.nb_port_desti,m_lue.nb_mess,calc);
			}
	
		}
		write(tube_verif[m_lue.nb_port_desti][1],&verif,sizeof(int)); //ecrit le 1 ou 0 ou 2 en fonction de si la valeur du recalcule est la même 
		printf("numero ecrit dans tube par transport : %d\n  ",verif);  
        free(m_lue.donnee);
        m_lue.donnee = NULL;
		sleep((rand()%3)+1);  //attente de 1 a 3 seconde
				
	}
	close(tube_mess_transp[0]); //ferme le tube apres la lecture
	for(int i = 0; i < n; i++) 
	{
		close(tube_verif[i][1]); // ferme les tubes ecritures apres avoir envoyer au client pour eviter que le read attende autre chose et se bloque
			//empeche l'execution du reste du main
	}
	free(m_lue.donnee);
	exit(0);
}
void traiter_message(msg_recons* msg, int num,int s) //lecture du message par le serveur 
{
	total_recus++;
	printf("Serveur %d reçoit message numéro %d sur port %d\n", s,msg->num_mess, num);
	printf("Taille reçue : %d\n", msg->taille_totale);
	
	
	unsigned int calcul_dans_serv = checksum_transport_serveur(msg->donnee,msg->taille_totale,msg->num_mess); //recalcule du checksum pour vérification 
	
	unsigned int total = calcul_dans_serv + msg->checksum;
	while (total >> 16)
	{
    total = (total & 0xFFFF) + (total >> 16);	
	}
	if (total== 0xFFFF) //compare les deux valeurs de checksum 
	{
		printf("le serveur a bien recu les données\n");
		total_valides++; //pour les statistiques des serveurs
	}
	else 
	{
		printf("les données sont corrompus\n");
		total_corrompus++; // pour les statistiques des serveurs
	}
    printf("le total (serveur) est %u\n",total);
}

void recevoir_fragments_dans_file(int msgid, msg_recons *msg,int port)
{
    
    int nb =0;
    file_m reception;// structure temporaire pour recevoir chaque fragment 
    
   
            
    int val = msgrcv(msgid,&reception,sizeof(file_m) - sizeof(long),port+1,0 ); //premier fragment 
    if (val == -1) 
    {
        perror("msgrcv");
        exit(1);
    } 
    if (reception.nb_fragments == -1)
    {
        msg->taille_totale = -1;// message spécial pour arrêter le serveur
    }
    else
    {
        msg->nb_fragments = reception.nb_fragments;
        msg->num_mess = reception.num_mess;
        msg->checksum = reception.checksum;

        msg->taille_totale = reception.taille_totale;

        msg->donnee =malloc(msg->taille_totale * sizeof(unsigned int));  // allocation mémoire pour stocker toutes les données du message complet
        for (int i = 0; i < reception.taille_fragment; i++)
        {
            msg->donnee[i] = reception.donnee[i]; //copie les données
        }
        msg->fragments_recus = 1; 
        while (msg->fragments_recus < msg->nb_fragments)// boucle pour recevoir les fragments restants
        {
            int val = msgrcv(msgid,&reception,sizeof(file_m) - sizeof(long),port+1,0 ); //suite des fragments 
            if (val == -1) 
            {
                perror("msgrcv");
                exit(1);
            }    

            int position = reception.fragment_id * 1000; // calcul de la position dans le tableau final

            for (int i = 0; i < reception.taille_fragment; i++)// copie des données du fragment à la bonne position
            {
                msg->donnee[position + i] = reception.donnee[i];
            }

            msg->fragments_recus++;
        
            nb++;
            if (nb==50)
            {
                printf("[WAI] fragment numero %d LUS\n",nb);
                fflush(stdout);
            }
        }
    }
}

void serveur(int s, int semid, struct sembuf P, struct sembuf V, int n, short *port, int m)
{
	
	printf("Serveur %d démarre\n", s);
	fflush(stdout);
	srand(time(NULL) ^ getpid()); //pour que chaque serveur est une graine de generation differente 
	sleep(rand()%6);
	semop(semid,&P,1); // prend le semaphore	
	int num = (rand()%n);//prend un numéro de port aléatoire
	printf("Serveur %d regarde port %d valeur = %d\n", s, num, port[num]);
	fflush(stdout);
	while (port[num]!=-1) //si port occupé il reprend un numéro aléatoire 
	{
		num = rand()%n;
	}
	
	port[num]=s;// si port libre il ecoute sur le port et le port prend le numéro du serveur 
	printf("Serveur %d écoute sur port %d\n",s,num);
	fflush(stdout);
	semop(semid,&V,1);// rend le semaphore 
    msg_recons msg;
    msg.donnee = NULL;
	key_t file_cle = ftok("cle_serv",1);
	if (file_cle==-1)
	{
		perror("ftok");
		exit(1);
	}
	int msgid = msgget(file_cle,0666);
	if(msgid ==-1)
	{
		perror("msgget");
		exit(1);
	}
	
	int arret_serv = 1;
	while(arret_serv)
	{
		
		fflush(stdout);// pour afficher direct 
		printf("[DEBUG] Serveur %d attend message de type %d\n", s, num+1);
		fflush(stdout);
		recevoir_fragments_dans_file(msgid,&msg,num);
		if (msg.taille_totale ==-1)  // pour arreter le serveur 
		{
			printf("Serveur %d termine\n",s);
			arret_serv=0; // pour arreter les serveurs quand il n'y a plus de messages à envoyer 
		}
		else
		{
			traiter_message(&msg,num,s);
            free(msg.donnee);
            msg.donnee = NULL;
			sleep(rand() % 5);   // attente aléatoire entre 0 et 4 secondes
		}
	}
	//affichage des statistiques des serveurs  
	printf("\n=== Statistiques Serveur %d ===\n", s);
    fflush(stdout);
	printf("Total reçus      : %d\n", total_recus);
    fflush(stdout);
	printf("Total valides    : %d\n", total_valides);
    fflush(stdout);
	printf("Total corrompus  : %d\n", total_corrompus);
	fflush(stdout);
    free(msg.donnee);
	exit(0);
}

int creer_semaphore(char *path, int id_cle, int valeur) // creation de semaphore 
{
    union semun 
	{
        int val;
    } arg;
    key_t cle = ftok(path, id_cle);
    if (cle == -1) // test d'erreur 
	{
        perror("ftok");
        exit(1);
    }
    int semid = semget(cle, 1, IPC_CREAT | 0666);
    if (semid == -1) // test d'erreur 
	{
        perror("semget");
        exit(1);
    }
    arg.val = valeur;
	int init = semctl(semid, 0, SETVAL, arg);
    if ( init == -1) //test d'erreur 
	{
        perror("semctl SETVAL");
        exit(1);
    }
    return semid;
}	

int main(int argc, char *argv[])
{
    FILE *f; //crée les fichiers utilisés dans ftock
	f = fopen("ipc_key", "a");
	fclose(f);

	f = fopen("cle_serv", "a");
	fclose(f);
    f = fopen("protec_w","a");
    fclose(f);
	if (argc <=3)
	{
		printf("Erreur : pas assez d'argument\n");
		exit(EXIT_FAILURE);
	}
	
	int n = atoi(argv[1]); //nb clients
	int m = atoi (argv[2]); // nb serveurs
	int d = atoi(argv[3]); // nb données
	int tube_mess_transp[2];// tube de client vers transport 1 seul
	if (m>n )
	{
		printf("Erreur trop de serveur par rapport au client\n");
		exit(1);
	}
	printf("sizeof(message) = %lu\n", sizeof(message));
    //création de la file de message
    key_t cle_file = ftok("cle_serv",1); 
    if (cle_file == -1) 
    {
        perror("ftok");
        exit(1); 
    }
    int msgid = msgget(cle_file,IPC_CREAT | 0666);
    if (msgid == -1) 
    {
        perror("msgget");
        exit(1); 
    }
    int semid_client = creer_semaphore("protec_w",1,1);
	pipe(tube_mess_transp); //création du tube 
	int tube_verif [n][2]; // tube de transport vers client , 1 par client
	for (int i = 0;i < n;i++)   //boucle car il faut créer les tubes un par un 
	{
		pipe(tube_verif [i]);   	
	}
	
	for (int i = 0 ;i<n; i++)   //processus client
	{
		int pid = fork();
		if (pid ==0 )  //entrée dans le fils
		{
            client(i, n, d, tube_verif, tube_mess_transp,semid_client);
        }
    }
    //creation de la mémoire partagée pour l'écoute des ports par les serveurs 
	key_t  cle= ftok("ipc_key",1);
	if (cle == -1) 
    {
        perror("ftok");
        exit(1); 
    }
	int shmid= shmget(cle,n*sizeof(short), IPC_CREAT |0666);
	if (shmid == -1) 
    {
        perror("shmget");
        exit(1); 
    }
	short * port =  shmat(shmid,NULL,0);  // NULL c'est le noyau qui choisis a quel espace memoire se branche le shmat et 0 donne les droits de lectures/ecritures $
	//processus transport 
	for(int i=0;i<n;i++)
    {
    	port[i] = -1; //initialisation des ports 
    }
    int pid_transport = fork();
	if (pid_transport > 0) // Le père ferme le pipe client transport 
    { 
    		close(tube_mess_transp[0]);
    		close(tube_mess_transp[1]);
	}
    if (pid_transport == 0)    //processus transport
	{
		transport (n,m,tube_mess_transp,port,tube_verif); 
	}

	int semid = creer_semaphore("ipc_key", 2, 1); // crée un sémaphore initialisé à 1
	struct sembuf V ={0,1,0};
	struct sembuf P ={0,-1,0};
	srand(time(NULL));
	
	printf("creation des serveur\n");
	fflush(stdout); // pour afficherr le printf et ne pas le laisser dans le buffer 
	
	for( int s = 0; s<m;s++)//creation des m serveurs
	{
		int pid_s = fork();
		if (pid_s ==0) //cree les processus serveur 
		{
			serveur(s,semid, P, V,n,port,m);
		}
	}
	
	for (int i = 0; i < n; i++) {
    	close(tube_verif[i][0]); // ferme tous les tubes créés 
    	close(tube_verif[i][1]);
	}
	
	
	
	for (int i = 0; i < n; i++)
	{
		wait(NULL); // attendre les n clients
	}
          

	wait(NULL);          // attendre le transport
	fermer_serveur(msgid, port, n);   // envoyer les messages FIN
	

	for (int i = 0; i < m; i++)
	{
		wait(NULL);      // attendre les m serveurs
	}
		msgctl(msgid, IPC_RMID, NULL); // suppression de la file de messages
		shmdt(port); // détachement mémoire partagée
		shmctl(shmid, IPC_RMID, NULL);// suppression mémoire partagée
		semctl(semid, 0, IPC_RMID);// suppression sémaphore
        semctl(semid_client,0,IPC_RMID);// suppression sémaphore
		exit(0);

}
