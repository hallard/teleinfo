
Repository dédié à la Téléinformation
=====================================

Ce dossier à la base était pour le programme téléinfo broadcast, mais il est devenu au fur et à mesure un peu le fourre tout de mes projets concernant la téléinformation.
Dorénavant, je crée des repos séparés pour les nouveeau projet 

Dans ce repo vous trouverez :
- Le dongle [Micro Teleinfo](https://github.com/hallard/teleinfo/tree/master/MicroTeleinfo)
- Le shield Raspberry [Pi PiTinfo](https://github.com/hallard/teleinfo/tree/master/PiTInfo)

Et le nouveau projet Teleinfo Wifi Serveur dans un repo dédié
- [WifInfo](https://github.com/hallard/WifInfo)

Historique de ce repo et programme associé ci-dessous :

Téléinfo Broadcast, mySQL et Emoncms
====================================
Ce programme qui permet de récupérer les trames téléinfo reçue par la liaison série d'un ordinateur (ou autre périphérique) puis de les envoyer (broadcaster) sur un reseau local. de ce fait, il est possible de récupérer ces informations sur une 
(ou même plusieurs machines) pour en faire le traitement souhaité.

Il est aussi capable d'envoyer les données de téléinfo vers une base mySQL ou un serveur Emoncms
 
Ce programme fonctionne uniquement sous linux (testé sur Debian Lenny et wheezy) mais çà ne doit pas être sorcier de l'adapter pour Windows.
 
Le fonctionnement est très simple, et peut être distingué en deux modes :
  - Le mode serveur (ou daemon), c'est à dire qu'il ecoute les trames reçues sur le port série et les transfère immediatement via une trame UDP sur le réseau local. Petite subtilité, en fait deux trames identiques sont envoyés, une sur le port 1200 et l'autre sur le port 1201, tout simplement parce que mon PC de domotique utilise deux programmes pour traiter les données de téléinfo or deux programmes distincts ne peuvent pas se écouter (binder) simultanément sur le même port.
  - mon daemon domotique avec l'affichage en temps réél des données téléinfo reçues
  - un script executé toutes les 5 minutes pour envoyer les données de téléinfo sur la base MySQL

  - Le mode client, c'est à dire qu'il écoute sur le réseau local une trame UDP (donc envoyée par de daemon) puis si celle-ci est valide, fait un traitement associé. Dans mon cas, soit on affiche la trame soit on stocke les données dans une base MySQL avec les valeurs reçues (le nom des champs de la table doit être le même que les champs de la trame reçue, donc un champs ADCO, …).

Suivez mes nouveautés et autres projets sur mon [blog][4] 

Installation et configuration
==============================

Tout est documenté sur la page dédié [teleinfo][5] de mon blog 

License
=======

<a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/"><img alt="Licence Creative Commons" style="border-width:0" src="https://i.creativecommons.org/l/by-nc-sa/4.0/88x31.png" /></a><br /><span xmlns:dct="http://purl.org/dc/terms/" property="dct:title">MicroTeleinfo</span> de <span xmlns:cc="http://creativecommons.org/ns#" property="cc:attributionName">Charles-Henri Hallard</span> est mis à disposition selon les termes de la <a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/">licence Creative Commons Attribution - Pas d’Utilisation Commerciale - Partage dans les Mêmes Conditions 4.0 International</a>.


[4]: http://hallard.me
[5]: http://hallard.me/teleinfo-emoncms/
