# Communication Inter-Processus (IPC) en C

Ce projet a été réalisé dans le cadre d'un TP de L2 en systèmes d'exploitation. L'objectif est de comparer trois mécanismes de communication inter-processus disponibles sous Linux : les files de messages, la mémoire partagée et les tubes (pipes). Chaque mécanisme a été implémenté deux fois, une première version traitant 2 000 données et une seconde traitant 2 000 000 données, afin d'observer le comportement du système à grande échelle.

## Pourquoi deux versions par mécanisme

Pour 2 000 données, les volumes restent suffisamment petits pour être envoyés en une seule opération, quel que soit le mécanisme utilisé. Pour 2 000 000 données en revanche, les limites imposées par le noyau rendent cette approche impossible. Il faut alors découper les données en plusieurs morceaux et les transmettre de façon itérative. Les deux versions permettent donc de comparer une implémentation simple à une implémentation adaptée au passage à l'échelle.

## Fichiers du projet

Le dossier files-messages contient prgfilemessage.c pour la version à 2 000 données et prg2millionsfilemessage.c pour la version à 2 millions. Le dossier memoire-partagee contient prgmemoirepartage2000.c et prg2millionsmemoirepartage.c. Le dossier tubes contient tube_2000.c et tube_2000000.c.

## Compilation et exécution

Les programmes se compilent avec gcc de la façon suivante : gcc tube_2000.c -o tube_2000, ou gcc prg2millionsmemoirepartage.c -o mem2millions. Tous les programmes nécessitent un environnement Linux avec le support POSIX. Certains programmes utilisent un modèle client/serveur et doivent être lancés dans deux terminaux séparés.
