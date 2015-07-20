Wifinfo : Wifi Teleinfo server
==============================

This is a server board for Teleinfo French Meter Measure Library, it can be used as a stand alone device web server that can display/send Teleinfo information.

You can see Teleinformation official french datasheet [there][1]

Since this is really dedicated to French energy measuring system, I will continue in French

Pour le moment le logiciel utilisé doit être considéré comme une version béta (voir alpha), même si le hardware est parfaitement fonctionnel, la partie logicielle est loin d'être terminée.

##Fonctionnalités Terminées
- Mise à jour par Wifi (pas besoin de cable) 
- Affichage dans l'interface Web des données de Teleinfo 
- Affichage dans l'interface Web des données système 

##Fonctionnalités à écrire
A terme les fonctionnalités de cette carte seront les suivantes :
- Créer la page WEB de connexion a un Wifi local 
- Créer la page WEB de configuration du Module 
- REST API pour effectuer des reqêtes afin d'obtenir les données de Téléinfo du type http://wifinfo/PAPP pour récupérer le 
contenu de l'étiquette PAPP (par exemple)
- Envoi des données de téléinfo vers EMONCMS
- Envoi des données de téléinfo vers les box domotique (JEEDOM, EEDOMUS, ...)
- D'autres idées arriveront surement en cours de toute...

##Schémas  
![schematic](https://raw.githubusercontent.com/hallard/teleinfo/master/Wifinfo/Wifinfo-sch.png)  

##Cartes 
<img src="https://raw.githubusercontent.com/hallard/teleinfo/master/Wifinfo/Wifinfo-brd.png" alt="board V1.0" width="30%" height="30%">&nbsp;
<img src="https://raw.githubusercontent.com/hallard/teleinfo/master/Wifinfo/Wifinfo-top.png" alt="top V1.0" width="30%" height="30%">&nbsp;
<img src="https://raw.githubusercontent.com/hallard/teleinfo/master/Wifinfo/Wifinfo-bot.png" alt="bottom V1.0" width="30%" height="30%">


##Documentation de la libraire LibTeleinfo
J'ai écrit un article [dédié][10] sur la librairie téléinfo qui est utilisée dans ce module. Vous pouvez aussi voir les [catégories][6] associées à la téléinfo sur mon [blog][7].

Pour les commentaires et le support vous pouvez allez sur le [forum][8] dédié ou dans la [communauté][9] 

##Sketch d'exemples
- [ESP8266_WEB][5] ESP8266 Web dynamic + Rest application, version en cours de développement.

##License
<a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-nc-sa/4.0/88x31.png" /></a><br /><span xmlns:dct="http://purl.org/dc/terms/" property="dct:title">Wifinfo</span> by <a xmlns:cc="http://creativecommons.org/ns#" href="https://hallard.me/max31865" property="cc:attributionName" rel="cc:attributionURL">Charles-Henri Hallard</a> is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/">Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License</a>.<br />Based on a work at <a xmlns:dct="http://purl.org/dc/terms/" href="https://github.com/hallard/MAX31865-Breakout" rel="dct:source">https://github.com/hallard/teleinfo/tree/master/Wifinfo</a>.

[1]: http://www.erdf.fr/sites/default/files/ERDF-NOI-CPT_02E.pdf
[2]: http://learn.adafruit.com/arduino-tips-tricks-and-techniques/arduino-libraries
[5]: https://github.com/hallard/LibTeleinfo/blob/master/Examples/ESP8266_WEB/ESP8266_WEB.ino
[6]: https://hallard.me/category/tinfo/
[7]: https://hallard.me
[8]: https://community.hallard.me/category/7
[9]: https://community.hallard.me
[10]: https://hallard.me/libteleinfo


