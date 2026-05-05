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
#include <sys/ipc.h>
#include <sys/shm.h>

//pour stat serveur 
int total_recus = 0;
int total_valides = 0;
int total_corrompus = 0;

struct Message  //composition des message
{
	short taille;
	short  nb_port_desti; //numéro port destination
	short   nb_mess;   //numéro message
	unsigned int  donnee[2000]; // donnée partagé
	short  checksum;
};
typedef struct Message message;

struct serv // pour le transport et serveur besoin de  la taille 
{
	short taille;
	unsigned int donnee[2000];
	short checksum;
	short num_mess;
};
typedef struct serv for_serv;
	
	
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
 	unsigned int somme = 0;
 

    somme += (unsigned short)mes->nb_port_desti;
    somme = (somme & 0xFFFF) + (somme >> 16);

    somme += (unsigned short)mes->nb_mess;
    somme = (somme & 0xFFFF) + (somme >> 16);

    somme += (unsigned short)mes->taille;
    somme = (somme & 0xFFFF) + (somme >> 16); 
 	return calcul_checksum(mes->donnee,d,somme);
}

unsigned short checksum_transport_serveur(for_serv *check,int d) // fonction pour calculer le checskum sans l'entête client 
{
    unsigned int somme =0;
	somme += (unsigned short) check->taille;
	somme = (somme & 0xFFFF) + (somme >> 16);
	somme+= (unsigned short) check->num_mess; // ajoute a somme l'entête crée par transport pour serveur
	somme = (somme & 0xFFFF) + (somme >> 16); 
    return calcul_checksum(check->donnee,d,somme);
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

void client (int i,int n,int d,int tube_verif[][2],int tube_mess_transp[2])
{
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
       
		generation_message(&mes, d,i,j);//génère les messages avec la fonction 		
        unsigned short checksum_client = checksum_client_transport(&mes,d);
		mes.checksum= ~checksum_client; //calcul du checksum
       
        printf("CLient %d envoie message %d\n",i,j); // pareil teste du programme 
        int verifi;
        int tentative_port =0; // variable pour la verification 
        do        // verification du checksum par transport et envoie de 0 ou 1, renvoie le message tant que 1
        {
            write(tube_mess_transp[1], &mes.taille, sizeof(short));
			write(tube_mess_transp[1], &mes.nb_port_desti, sizeof(short));
			write(tube_mess_transp[1], &mes.nb_mess, sizeof(short));
			write(tube_mess_transp[1], mes.donnee, d * sizeof(unsigned int));
			write(tube_mess_transp[1], &mes.checksum, sizeof(short));// renvoie le message quand la verification est incorrect 
			total_messages_envoyes++; // pour les statistiques		
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
				fflush(stdout);
				printf("\n=== STATISTIQUES CLIENT  ===\n");
				fflush(stdout);
				printf("Client %d , Messages envoyés : %d\n",i, total_messages_envoyes);
				fflush(stdout);
				printf("Client %d , Entiers envoyés : %d\n",i, total_messages_envoyes * 2000);
				fflush(stdout);
				printf("CLient %d , Message échoués : %d\n",i,message_echoués);
				fflush(stdout);
				exit(1);
            }
					
			sleep((rand() % 3) + 1);  // 1 à 3 secondes
		}while (verifi !=0);
		sleep(rand()%6);	//attente de 0 a 5 seconde 
	}
	close(tube_verif[i][0]);//fermeture des tubes quand le client a fini
	close(tube_mess_transp[1]);// pareil 
	// affichage des stats
	printf("\n=== STATISTIQUES CLIENT  ===\n");
	fflush(stdout);
	printf("Client %d , Messages envoyés : %d\n",i, total_messages_envoyes);
	fflush(stdout);
	printf("Client %d , Entiers envoyés : %d\n",i, total_messages_envoyes * 2000);
	fflush(stdout);
	printf("CLient %d , Message échoués : %d\n",i,message_echoués);
	fflush(stdout);
	exit(0);	//exit 
}

void fermer_serveur(int m)
{
	for (int s = 0; s < m; s++) //pour fermer tout les serveurs une fois que tous les messages ont été envoyer 
	{

		key_t cle = ftok("cle_serv", s+1);// pour récupérer la clé de la zone de memoire partagé
		int shmid = shmget(cle, sizeof(for_serv), 0666); // pour acceder a la zone de mémoire partagé 
		if (shmid == -1) //test d'erreur 
		{
   			perror("Erreur shmget");
    		exit(1);
		}
		for_serv *ptr = shmat(shmid, NULL, 0);
		if(ptr==(void *) -1) //car shmat retourne un pointeur vers la zone mémoire attaché 
		{
			perror("Erreur shmat");
			exit(1);
		}
		ptr->taille = -1; // pour indiquer qu'il n'y a plus de messsages 

		shmdt(ptr);

		key_t cle_sem_serv = ftok("cle_serv", s + m + 1);
		int semid_serv = semget(cle_sem_serv, 1, 0666);

		struct sembuf V = {0,1,0};
		semop(semid_serv, &V, 1);
		semctl(semid_serv, 0, IPC_RMID);
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
void ecrire_mem_partagee(for_serv *zone,message *m_lue)
{
	
	zone->taille =m_lue->taille; //cree la taille car le transport ne la connais pas 
	zone->num_mess = m_lue->nb_mess; 
	
	for (int i = 0; i < m_lue->taille; i++)
	{
		zone->donnee[i] = m_lue->donnee[i]; //ecris toute les données dans la zone de mémoire partagée
	}
	unsigned int checksum_serveur = ~checksum_transport_serveur(zone,m_lue->taille);// ecrit le chekcsum dans la zone de memoire partagée
	zone->checksum = checksum_serveur;
}

void liberer_semaphore(int serveur_id, int m)
{
	struct sembuf V ={0,1,0}; 
	key_t cle_sem_serv = ftok("cle_serv", serveur_id + m + 1); // cle pour le semaphore
	int semid_serv = semget(cle_sem_serv, 1, 0666);
	semop(semid_serv,&V,1);// primitive V je libere un semaphore pour que le serveur puisse lire le message 
    							
}
void transport (int n,int m,int tube_mess_transp[2],short port[],int tube_verif[][2])

{
	srand(time(NULL)); 
	message m_lue;  //variable pour stocker la structure envoyer
	close(tube_mess_transp[1]); // fermeture de l'ecriture 
	for (int i = 0;i<n;i++)
	{
		close(tube_verif[i][0]); //ferme tube de lecture de la verif de tous les clients 
	}
	int verif;
	while (read(tube_mess_transp[0],&m_lue.taille,sizeof(short)) > 0)
	{
    	read(tube_mess_transp[0], &m_lue.nb_port_desti, sizeof(short));
    	read(tube_mess_transp[0], &m_lue.nb_mess, sizeof(short));
    	read(tube_mess_transp[0], m_lue.donnee,m_lue.taille * sizeof(unsigned int));
    	read(tube_mess_transp[0], &m_lue.checksum, sizeof(short));

		short serveur_id = port[m_lue.nb_port_desti];
								
		if (serveur_id == -1)
		{
			verif= 2; // port n'est pas écouté par un serveur
			write(tube_verif[m_lue.nb_port_desti][1],&verif,sizeof(int));
		}
		else 
		{
			//acces a la zone memoire partagé voulu depuis le transport			
			key_t cle = ftok("cle_serv", serveur_id+1);
			int shmid = shmget(cle, sizeof(for_serv), 0666);
			if (shmid == -1) 
			{
   				perror("Erreur shmget");
    			exit(1);
			}
			verif = verif_checksum(&m_lue); 
			if (verif==0)
			{
				
				for_serv *ptr_vers_zone =  shmat(shmid, NULL, 0);  // attache a la zone mémoire 
				if(ptr_vers_zone==(void *) -1) //car shmat retourne un pointeur vers la zone mémoire attaché 
				{
					perror("Erreur shmat");
					exit(1);
				}
				ecrire_mem_partagee(ptr_vers_zone, &m_lue);
				shmdt(ptr_vers_zone); //detachement 
				liberer_semaphore(serveur_id,m);
			}
			else 
			{
				verif = 1;//envoie 1 donc valeur du recalcule différent 
			}
			 
			write(tube_verif[m_lue.nb_port_desti][1],&verif,sizeof(int)); //ecrit le 1 ou 0 en fonction de si la valeur du recalcule est la meme 
			printf("numero ecrit dans tube par transport : %d\n  ",verif); // pour test 
			sleep((rand()%3)+1);  //attente de 1 a 3 seconde
				
		}
				
	}
	close(tube_mess_transp[0]); //ferme le tube apres la lecture
	for(int i = 0; i < n; i++) 
	{
		close(tube_verif[i][1]); // ferme les tubes ecritures apres avoir envoyer au client pour eviter que le read attende autre chose et se bloque
			//empeche l'execution du reste du main
	}
	fermer_serveur(m);
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

void traiter_message(for_serv* memoire_serveur, int num,int s) //lecture du message par le serveur 
{
	total_recus++;
	printf("Serveur %d reçoit message numéro %d sur port %d\n", s,memoire_serveur->num_mess, num);
	printf("Taille reçue : %d\n", memoire_serveur->taille);
	
	int d = memoire_serveur->taille;
	unsigned int calcul_dans_serv = checksum_transport_serveur(memoire_serveur, d); //recalcule du checksum pour vérification 
	
	unsigned int total = calcul_dans_serv + (unsigned short)memoire_serveur->checksum;
	while (total >> 16)
	{
    total = (total & 0xFFFF) + (total >> 16);	
	}
	printf("le total (serveur) est %u\n",total);
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
}

for_serv* init_memoire_serveur(int s,char *path)   //cree la memoire partager de chaque serveur 
{
    key_t cle_serv = ftok(path, s + 1);
    int shmid = shmget(cle_serv, sizeof(for_serv), IPC_CREAT | 0666);
    if (shmid == -1) //test d'erreur 
	{
		perror("shmget");
		exit(1); 
	}
    for_serv* memoire_serveur = shmat(shmid, NULL, 0);
    if (memoire_serveur == (void*) -1) //test d'erreur 
	{ 
		perror("shmat"); 
		exit(1); 
	}
    return memoire_serveur;
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

	//creation de la zone de memoire partagé 
	for_serv * memoire_serveur = init_memoire_serveur(s,"cle_serv");

	//creation semaphore par serveur
	int semid_serv = creer_semaphore("cle_serv", s + m + 1, 0);
	int arret_serv = 1;
	while(arret_serv)
	{
		
		fflush(stdout);// pour afficher direct 
		semop(semid_serv,&P,1);// primitive P lis dans la memoire partage pour comparer les checksum 	
		if (memoire_serveur->taille ==-1)  // pour arreter le serveur 
		{
			printf("Serveur %d termine\n",s);
			arret_serv=0; // pour arreter les serveurs quand il n'y a plus de messages à envoyer 
		}
		else
		{
			traiter_message(memoire_serveur,num,s);
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
	exit(0);

}
int main(int argc, char* argv[])
{
	FILE *f; //crée les fichiers utilisés dans ftock
	f = fopen("ipc_key", "a");
	fclose(f);

	f = fopen("cle_serv", "a");
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
            client(i, n, d, tube_verif, tube_mess_transp);
        }
    }
    //creation de la mémoire partagée pour l'écoute des ports par les serveurs 
	key_t  cle= ftok("ipc_key",1);
	
	int shmid= shmget(cle,n*sizeof(short), IPC_CREAT |0666);
	
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
	
	
	
	for (int i = 0; i < n+1+m; i++) // evite le zombie 
	{
    	wait(NULL);
    		
	}
	shmdt(port); // a la fin du programme supprime les valeur ipcs 
	for (int s = 0; s < m; s++)
	{
    	key_t cle_serv = ftok("cle_serv", s + 1);
    	int shmid_serv = shmget(cle_serv, sizeof(for_serv), 0666);
    
    	if (shmid_serv != -1)
    	{
        	shmctl(shmid_serv, IPC_RMID, NULL);
    	}
	}
	shmctl(shmid, IPC_RMID, NULL);
	semctl(semid, 0, IPC_RMID);
	
	exit(0);

}