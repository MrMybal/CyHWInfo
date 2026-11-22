---
id: monitoring
title: Surveiller le matériel
description: Lire le tableau Capteurs et activer les collecteurs Windows facultatifs.
section: CyHWInfo
parent: overview
order: 10
sourceRevision: 1
---

## Capteurs et statistiques {#sensor-table}

Déplie un périphérique ou un groupe de mesures dans **Capteurs**. Le tableau affiche **Actuel**, **Minimum**, **Maximum** et **Moyenne**. Utilise le champ de recherche pour retrouver un périphérique ou un capteur. **Mesures disponibles** masque les lignes sans relevé actuel. Survole un capteur pour voir sa source.

```image
{"id":"sensor-table-example","src":"assets/sensors-fr.png","alt":"Tableau des capteurs avec les pourcentages des états C par cœur et leurs statistiques sur des données de démonstration","caption":"Tableau Capteurs avec des données de démonstration","controls":"minimal"}
```

Les statistiques portent sur les relevés valides depuis le début de la session ou la dernière **Réinitialisation des statistiques**. Chaque lecture SMART réelle compte une seule fois, même si le collecteur réutilise sa valeur. La pause suspend les relevés. La réinitialisation efface les statistiques et conserve la valeur actuelle. Un tiret indique une donnée indisponible. Zéro est une valeur valide, par exemple pour un ventilateur arrêté. Les indicateurs binaires affichent Oui ou Non et mémorisent les états observés.

## Capteurs avancés sous Windows {#advanced-sensors}

1. Compile le composant `LibreHardwareMonitor` et déploie le dossier `cyhwinfo-lhm` à côté de l’exécutable. Installe le runtime .NET 8 requis par le collecteur.
2. Pour les relevés qui nécessitent un accès au matériel, installe le [pilote signé PawnIO](https://pawnio.eu/), distribué séparément. La session Windows doit pouvoir accéder à son périphérique. Des droits administrateur peuvent être nécessaires.
3. Dans l’application, active **Paramètres → Capteurs avancés**. Désactive cette option pour continuer avec les collecteurs standard.

Les relevés disponibles dépendent du matériel, du firmware, des pilotes et des droits d’accès. LibreHardwareMonitor peut fournir les températures et la puissance CPU ainsi que les tensions, températures et vitesses des ventilateurs de la carte mère. Sur les AMD Raphael pris en charge, le collecteur facultatif ajoute les timings actifs du contrôleur DDR5 et les indicateurs HTC, PROCHOT interne et PROCHOT externe. La table SMU `0x00540004` ajoute les températures par cœur, les limites et l’utilisation PPT/TDC/EDC, les débits DRAM en lecture et en écriture et les résidences C0, C1 et C6 par cœur.

La valeur actuelle du **Nombre moyen de cœurs actifs** additionne les fractions de temps passées en C0 par les cœurs physiques pendant un même intervalle SMU. Deux cœurs qui passent chacun la moitié de cet intervalle en C0 comptent pour un cœur actif en moyenne. La colonne **Moyenne** calcule la moyenne de ces relevés sur la session.
