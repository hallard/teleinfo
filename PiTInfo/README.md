Pi Téléinfo
===========
Shield Teleinfo pour Raspberry PI permettant de récupérer les trames téléinformation

Suivez mes nouveautés et autres projets sur mon [blog][4] 

Installation et configuration
==============================

Attention, carte non testée mais vu la simplicité du PCB çà devrait être bon, je validerais dès les cartes de test reçues.

La led est au choix (en CMS ou à trous) car dans certains boitiers je ne suis pas sur qu'une led 3MM loge, du coup vous avez le choix ou non de l'installer. Vous pouvez le pas la mettre, c'est facultatif pour le visuel. Si vous ne mettez pas la LED R2 ne sert à rien.

Je suis certain que la résistance R1 ne sert à rien, je me souviens l'avoir enlevée sur la carte [ArduiPi][2] car à l'epoque avec une 10K le signal était trop faible pour l'entrée RX du Pi et çà ne fonctionnait pas. En l'enlevant tout fonctionnait. Peut être qu'avec une 3.3K çà fonctionne aussi.

Je mettrais le programme [Teleinfo Broacast][3] à jour pour faire clignoter la LED quand une trame est reçue.

Tout est documenté sur la page dédié [teleinfo][5] de mon blog 

License
=======

<a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/"><img alt="Licence Creative Commons" style="border-width:0" src="https://i.creativecommons.org/l/by-nc-sa/4.0/88x31.png" /></a><br /><span xmlns:dct="http://purl.org/dc/terms/" property="dct:title">PiTInfo</span> de <span xmlns:cc="http://creativecommons.org/ns#" property="cc:attributionName">Charles-Henri Hallard</span> est mis à disposition selon les termes de la <a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/">licence Creative Commons Attribution - Pas d’Utilisation Commerciale - Partage dans les Mêmes Conditions 4.0 International</a>.

Conception
==========


**Schémas**  
![schematic](https://raw.githubusercontent.com/hallard/teleinfo/master/PiTInfo/PiTlnfo-sch.png)

![board]( https://raw.githubusercontent.com/hallard/teleinfo/master/PiTInfo/PiTlnfo-brd.png )

**Circuit Imprimé**  
![top](https://raw.githubusercontent.com/hallard/teleinfo/master/PiTInfo/PiTlnfo-top.png)&nbsp;&nbsp;![bottom](https://raw.githubusercontent.com/hallard/teleinfo/master/PiTInfo/PiTlnfo-bottom.png)

[2]: http://hallard.me/arduipi-the-shield-that-brings-arduino-to-raspberry-pi/
[3]: http://hallard.me/teleinfo-emoncms/
[4]: http://hallard.me
[5]: http://hallard.me/teleinfo/
