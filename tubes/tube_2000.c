#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>  

struct Message{ // les objets sont volontairement choisis comme unsigned en plus du short (pour ceux demandés) au vu du fait que les valeurs ne peuvent pas être négative par cohérence avec le sujet

	unsigned short taille;//taille données
        unsigned short num_port_desti; //numéro port destination
        unsigned short num_mess;//numéro message
        unsigned int  donnee[2000]; 
        unsigned short checksum;

};
typedef struct Message message;

struct Serveur{

	unsigned int donnee[2000];
	unsigned short checksum_serv;

};
typedef struct Serveur message_serveur;


unsigned short calcul_checksum(unsigned int *data ,int d,unsigned int somme){ //fonction du calcul de checksum de base pour pas recopier plusieurs fois la même fonction


	for (int s = 0;s<d;s++){

		unsigned short  moitie1 = (data[s] >>16) & 0xFFFF; //premier moitie des 32 bits d'une donnée car on met sur 16 bits
		unsigned short  moitie2 = data[s] & 0xFFFF; // la deuxieme moitie pour une donnée car on met sur 16 bits
		
		somme += moitie1;   
		somme = (somme & 0xFFFF) + (somme >> 16);  // gère la retenu en prenant les 16 derniers bits et en ajoutant ce qui dépasse
		somme += moitie2; 
		somme = (somme & 0xFFFF) + (somme >> 16); //gère la retenu en prenant les 16 derniers bits et en ajoutant ce qui dépasse
	}
	return (unsigned short)(somme & 0xFFFF);	
}	



unsigned short checksum_client_transport(message *mes,int d){ //fonction pour calculer le checksum client 

       	unsigned int somme = mes->num_port_desti +  mes->num_mess + mes->taille;//ajoute a la somme l'entête client

	return calcul_checksum(mes->donnee,d,somme);//on ajoute les entetes spécifiques au calcul du checksum
}



void generation_message(message *mes, int d, int port_dest, int num_mess){ // fonction pour la génération des messages 
  	
    mes->num_port_desti = port_dest;
    mes->num_mess = num_mess;
    mes->taille = d;
    for (int k = 0; k < d; k++){
        mes->donnee[k] = (unsigned int) rand();
    }
}


void client (int i,int n,int d,int tube_verif[][2],int tube_mess_transp[2]){
	
	unsigned int total_messages_envoyes = 0;
	unsigned int message_echoués=0;
    	for (int k = 0; k < n; k++){

    		if (k != i){  //le client i ne garde que son propre tube donc ferme ceux des autres clients
    	
    			close(tube_verif[k][0]);
    			close(tube_verif[k][1]);
    	        }
    }
    close(tube_mess_transp[0]); //ferme le tube en mode lecture  car le client écris vers transport
    close(tube_verif[i][1]);//ferme le tube en mode écriture car le client reçoit la réponse du transport donc il n'écrit pas
				
    srand(123456 + i);
			
    for (int j = 0; j< 10;j++){  //boucle pour les messages
								
        message mes; // declaration du message du fils i 
       
	generation_message(&mes,d,i,j);//génère les messages avec la fonction
	unsigned short checksum_client = checksum_client_transport(&mes,d); 		
        mes.checksum = ~checksum_client;//calcul du checksum avec toutes les informations d'entetes client




        printf("checksum envoyer par client vers transport:%hu\n",mes.checksum); // test du programme 
        printf("CLient %d envoie message %d\n",i,j); // pareil teste du programme 
    



	unsigned int verifi;//nb lue dans le tube envoyé par transport
        unsigned int tentative_port =0; //si verifi == 2 , le nb de tentative augmente
        do{

           write(tube_mess_transp[1], &mes.taille, sizeof(unsigned short));
	   write(tube_mess_transp[1], &mes.num_port_desti, sizeof(unsigned short));
	   write(tube_mess_transp[1], &mes.num_mess, sizeof(unsigned short));
	   write(tube_mess_transp[1], mes.donnee, d * sizeof(unsigned int));
	   write(tube_mess_transp[1], &mes.checksum, sizeof(unsigned short));
		// renvoie le message quand la verification est incorrect

	    total_messages_envoyes++; // pour les statistiques	

            read(tube_verif[i][0],&verifi,sizeof(unsigned int)); // lit le message du transport : 1 ou 0
        
	    printf("numero dans tube lus client : %u\n  ",verifi); // pour le test aussi
            
	    
	    if (verifi ==1){ // si 1 alors le calcul du checksum du transport est differents de celui du client, on considerera que si le calcul n'est pas bon alors on exit les clients pour arreter le programme  

                exit(1);
            
	    }
            else if (verifi ==2){  // test si port sur écoute par un serveur 
          
                tentative_port++;
                printf("Aucun serveur. Tentative %u/5 pour le client %d\n", tentative_port,i);
		message_echoués++;
            }

            if (tentative_port >= 5){
            
                printf("Port indisponible après 5 tentatives\n");
		fflush(stdout);
		printf("\n=== STATISTIQUES CLIENT  ===\n");
		fflush(stdout);
		printf("Client %d , Messages envoyés : %u\n",i, total_messages_envoyes);
	        fflush(stdout);	
		printf("Client %d , Entiers envoyés : %u\n",i, total_messages_envoyes * 2000);
		fflush(stdout);
		printf("CLient %d , Message échoués : %u\n",i,message_echoués);
		fflush(stdout);
                exit(0);
            }
					
	    sleep((rand() % 3) + 1);  // 1 à 3 secondes

	}while (verifi !=0);


	sleep(rand()%6);	//attente de 0 a 5 seconde 
	


     }


	close(tube_verif[i][0]);//fermeture des tubes quand le client a fini
	close(tube_mess_transp[1]);// idem 
	// affichage des stats
	printf("\n=== STATISTIQUES CLIENT  ===\n");
	fflush(stdout);
	printf("Client %d , Messages envoyés : %u\n",i, total_messages_envoyes);
	fflush(stdout);
	printf("Client %d , Entiers envoyés : %u\n",i, total_messages_envoyes * 2000);
	fflush(stdout);
	printf("CLient %d , Message échoués : %u\n",i,message_echoués);
	fflush(stdout);
	printf("\n");

        exit(0);
}



int verif_checksum_transport(message *m){

    unsigned int calc = checksum_client_transport(m,m->taille);

    unsigned int total = calc + (unsigned short)m->checksum;

    while (total >> 16){

  	    total = (total & 0xFFFF) + (total >> 16);
    }
    printf("le total (transport) est %u\n",total);
    return (total == 0xFFFF) ? 0 : 1;
}

int verif_checksum_serveur(message_serveur *mess_serv,int d){

	unsigned int result_donnee = calcul_checksum(mess_serv->donnee,d,0);
        unsigned int total = result_donnee + (unsigned short)mess_serv->checksum_serv;

	while(total >> 16){

		total = (total & 0xFFFF) + (total >> 16);
	}
	printf("le total (serveur) est %u\n",total);
	return (total == 0xFFFF) ? 0 : 1;

}

void ecrire_tube_trans_serv(message* m_lue,short serveur_id,int tube_trans_serv[][2]){
	//ecrit dans le tube du serveur concerné les données puis le checksum recalculé sans les entetes client à l'exception de taille 
	write(tube_trans_serv[serveur_id][1],m_lue->donnee,m_lue->taille * sizeof(unsigned int));
	unsigned short new_checksum = ~calcul_checksum(m_lue->donnee,m_lue->taille,0);// = complément à 1 du checksum sans entetes client que serveur en le recalculant comparera à 0xFFFF

	write(tube_trans_serv[serveur_id][1],&new_checksum,sizeof(unsigned short));
			
}




void transport (int n,int m,int tube_mess_transp[2],short port[],int tube_verif[][2],int tube_trans_serv[][2]){

	srand(time(NULL) * getpid());
	message m_lue;

	close(tube_mess_transp[1]); // transport n'écrit pas dans le tube commun avec les clients

	for (int i = 0;i<n;i++){

		close(tube_verif[i][0]); //ferme tube de lecture de la verif de tous les clients
	}

	for (int i=0;i<m;i++){//transport ferme tubes serveurs coté lecture car ne fait que écrire
		close(tube_trans_serv[i][0]);
	}

	unsigned int verif;
	
	while(read(tube_mess_transp[0],&m_lue.taille,sizeof(unsigned short))>0){ // continue jusqu'a ce qu'il ny est plus aucun message dans le tube
		
		read(tube_mess_transp[0], &m_lue.num_port_desti, sizeof(unsigned short));
    		read(tube_mess_transp[0], &m_lue.num_mess, sizeof(unsigned short));
    		read(tube_mess_transp[0], m_lue.donnee,m_lue.taille * sizeof(unsigned int));
    		read(tube_mess_transp[0], &m_lue.checksum, sizeof(unsigned short));
				
		//ici transport a reconstruit le message à partir de read succesif des octets dans le tube afin de reconsruire le message coté transport, celui-ci a donc reconstruit le structure sans en connaitre à priori la taille 
		
		short serveur_id = port[m_lue.num_port_desti];//acces à la mémoire partagé pour savoir si le port du client est en ecoute
						 
		if (serveur_id == -1){

				verif= 2; // port n'est pas écouté par un serveur

		}
		else{

			
			verif = verif_checksum_transport(&m_lue);//on verifie le checksum client par le transport
			if (verif==0){
				ecrire_tube_trans_serv(&m_lue,serveur_id,tube_trans_serv);
			}


		}
		write(tube_verif[m_lue.num_port_desti][1],&verif,sizeof(unsigned int)); //ecrit le 1 ou 0 ou 2 en fonction de si la valeur du recalcule est la meme
		printf("numero ecrit dans tube par transport : %d\n  ",verif); // pour test
		sleep((rand()%3)+1);  //attente de 1 a 3 seconde entre 2 lectures 
	
	}
	close(tube_mess_transp[0]); //ferme le tube commun  clients apres la lecture
	for(int i = 0; i < n; i++){

		close(tube_verif[i][1]); // ferme les tubes ecritures apres avoir envoyer au client pour eviter que le read attende autre chose et se bloque
		//empeche l'execution du reste du main

	}
	
	for(int i=0;i<m;i++){

		close(tube_trans_serv[i][1]);//ferme les tubes serveurs après que transport n'est plus rien à lire coté client donc que ceux-ci sont finis et donc n'ont plus rien à recevoir des serveurs

	}
	exit(0);//fin de transport



}



int creer_semaphore(char *path, int id_cle, int valeur){ // creation de semaphore
	
    union semun{
        int val;
    } arg;

    key_t cle = ftok(path, id_cle);
    if (cle == -1){ // test d'erreur
        perror("ftok");
        exit(1);
    }

    int semid = semget(cle, 1, IPC_CREAT | 0666);
    if (semid == -1){ // test d'erreur
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



void serveur(int s, int semid, struct sembuf P, struct sembuf V, int n, short *port, int m,int d,int tube_trans_serv[][2]){

	for (int i=0;i<m;i++){//on ferme pour 1 serveur tout les descripteur du tube des autres serveurs 
		if (i != s){
			close(tube_trans_serv[i][0]);
			close(tube_trans_serv[i][1]);
		}
	}
	close(tube_trans_serv[s][1]);//ferme ecriture pour le tube serveur (inutile)


	printf("Serveur %d démarre\n", s);
	fflush(stdout);
	srand(time(NULL) * getpid()); //pour que chaque serveur aient une graine de generation differente
	sleep(rand()%6);
	semop(semid,&P,1); // prend le semaphore
	int num = (rand()%n);//prend un numéro de port client aléatoire
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
			  
	//les serveurs auront pour s'arreter un read à -1 

	message_serveur serv;//structure pour réception donnees + checksum 

	unsigned int total_recus = 0;//nb de messages reçus
	unsigned int total_valides = 0;//messages avec checksum ok
	unsigned int total_corrompus = 0;//messages avec checksum faux
	unsigned int verif;//valeur booléenne de réussite du checksum

	while (read(tube_trans_serv[s][0],serv.donnee,d * sizeof(unsigned int))>0){
		read(tube_trans_serv[s][0],&serv.checksum_serv,sizeof(unsigned short));
		total_recus++;

	//on met d ici car serveur connait normalement sans entete la taille des donnees
		if((verif = verif_checksum_serveur(&serv,d)) == 0){
			total_valides++;
		}
		else{
			total_corrompus++;
		}
		
		sleep(rand() % 5);   // attente aléatoire entre 0 et 4 secondes
	}

	//affichage des statistiques des serveurs
	printf("\n=== Statistiques Serveur %d ===\n", s);
	fflush(stdout);
	printf("Total reçus      : %u\n", total_recus);
	fflush(stdout);
	printf("Total valides    : %u\n", total_valides);
	fflush(stdout);
	printf("Total corrompus  : %u\n", total_corrompus);
	fflush(stdout);
	printf("\n");

	//fin stat
	//fermeture tube du serveur puis exit
	close(tube_trans_serv[s][0]);
	exit(0);


}

int main(int argc,char* argv[]){

	//------INIT-------
	

	if (argc <=3){

		printf("Erreur : pas assez d'argument\n");
		exit(EXIT_FAILURE);
	}

	
	int n = atoi(argv[1]); //nb clients
	int m = atoi (argv[2]); // nb serveurs
	int d = atoi(argv[3]); // nb données, 2000 dans ce cas, ce choix est motivé par soucis de lisibilité avec une variable plutot que de mettre 2000 de façon éparpillé
	int tube_mess_transp[2];// tube de client vers transport 1 seul
	if (m>n){

		printf("Erreur trop de serveur par rapport au client\n");
		exit(1);
	}

	pipe(tube_mess_transp); //création du tube 
	int tube_verif [n][2]; // tube de transport vers client , 1 par client

	for (int i = 0;i < n;i++){ //boucle car il faut créer les tubes un par un
		pipe(tube_verif [i]);
	}


	// FIN INIT
	// ------CLIENT------


	for (int i = 0 ;i<n; i++){  
		
		int pid = fork();
		
		if (pid ==0 ){  
		
           		 client(i, n, d, tube_verif, tube_mess_transp);
       		 }
    	}



	//FIN CLIENT

	//-----Creation de la mémoire partagée pour l'écoute des ports par les serveurs ------
	

	FILE *f; //crée les fichiers utilisés dans ftock pour la mémoire partagé des serveurs                                                                   
	f = fopen("mem_serv", "a");// on ouvre en appending de façon arbitraire juste pour créer le fichier si il n'existe pas, il ne servira que de clépour ftock                                                                         
	if (f == NULL){
		perror("fichier (mem_serv) non créé/ouvert");
		exit(1);	
	}
       	fclose(f);


	//pour ipc_key

	 FILE *f2; //crée fichier utilisés dans ftock pour le semaphore d'accès à la mémoire partagé
        f2 = fopen("ipc_key", "a");// on ouvre en appending de façon arbitraire juste pour créer le fichier si il n'existe pas, il ne servira que de clépour ftock
        if (f2 == NULL){
                perror("fichier (ipc_key)  non créé/ouvert");
                exit(1);
        }
        fclose(f2);
	
	
	//fin : pour ipc_key


	key_t  cle= ftok("mem_serv",1);
	if (cle == -1){
     	   perror("ftok");
       	   exit(1);
    	}

	int shmid= shmget(cle,n*sizeof(short), IPC_CREAT|0666);//0666 = lecture + ecriture à toutes personnes sur la machine

	if (shmid == -1){
		perror("erreur memoire partagé (shmget)");
		exit(1);
	}


	// attachement de la mémoire partagé au processus vu comme un tableau d'entiers de 2 octets
	
	short * port =  shmat(shmid,NULL,0);  // NULL c'est le noyau qui choisis a quel espace memoire se branche le shmat et 0 donne les droits de lectures/ecritures au processus

	if (port == (void *) -1){
		perror("erreur memoire partagé (shmat)");
		exit(1);
	}

	
	for(int i=0;i<n;i++){
   	 	port[i] = -1; //initialisation des ports 
	}


	// FIN CREATION MEMOIRE PARTAGE
	
	
	//CREATION TUBES TRANSPORT VERS SERVEURS
	
	int tube_trans_serv[m][2];
	for (int i=0;i<m;i++){
		pipe(tube_trans_serv[i]);
	}

	//FIN CREATION TUBES TRANSPORT VERS SERVEURS

	// -----TRANSPORT------


	int pid_transport = fork();
	if (pid_transport > 0) { // Le père ferme le pipe client→transport (inutile pour lui)
    		close(tube_mess_transp[0]);
    		close(tube_mess_transp[1]);
	}
    	if (pid_transport == 0){   
			      
		transport (n,m,tube_mess_transp,port,tube_verif,tube_trans_serv); 
	}


	//FIN TRANSPORT
	//----CREATION SEMAPHORE----
	//on créer sémaphore pour gestion d'accès à la mémoire partagé 
	//risque d'écriture concurente = pourrait corompre port attriubuer
	//au mauvais serveur
	

	int semid = creer_semaphore("ipc_key", 2, 1); // crée un sémaphore initialisé à 1
	struct sembuf V ={0,1,0};
	struct sembuf P ={0,-1,0};

	//FIN CREATION SEMAPHORE
	//-------CREATION SERVEURS--------

	printf("creation des serveur\n");
	fflush(stdout); // pour afficherr le printf et ne pas le laisser dans le buffer
	
	for( int s = 0; s<m;s++){//creation des m serveurs
		int pid_s = fork();
		if (pid_s ==0) //cree les processus serveur
		{
			serveur(s,semid, P, V,n,port,m,d,tube_trans_serv);
		}
	
	
	}


	//FIN SERVEURS
	//TERMINAISON
	
	for(int i=0;i<m;i++){//le père ferme le pipe transport->serveurs
                        close(tube_trans_serv[i][0]);
		       	close(tube_trans_serv[i][1]);
                }
	
	for (int i = 0; i < n; i++) { 
    	       close(tube_verif[i][0]); // ferme les tubes restants de client<->transport  
    	       close(tube_verif[i][1]);
	}
		
	for (int i = 0; i < n+1+m; i++){ // evite le zombie

    		wait(NULL);

	}

	shmdt(port); // a la fin du programme supprime les valeur ipcs
	shmctl(shmid, IPC_RMID, NULL);
	semctl(semid, 0, IPC_RMID);
	exit(0);


	//FIN DU PROGRAMME		

}



