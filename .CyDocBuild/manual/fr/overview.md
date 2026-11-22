---
id: overview
title: Premiers pas
description: Choisir entre le moniteur Windows et la bibliothèque C++.
section: CyHWInfo
order: 0
sourceRevision: 1
---

## Ce que tu peux intégrer {#what-you-can-build}

CyHWInfo est une bibliothèque C++23 modulaire pour l’inventaire matériel et la lecture des capteurs. Lie les composants dont ton application a besoin, par exemple l’inventaire et les mesures GPU. Le module CyUI facultatif fournit un tableau de bord intégrable. Le moniteur Windows autonome utilise le même tableau de bord et les mêmes collecteurs.

## Ouvrir le moniteur {#open-the-monitor}

Dans une version Windows de l’application, lance `CyHWInfo.exe`. L’application ouvre la page **Capteurs**. Déplie un composant pour voir les périphériques et leurs relevés. Le menu **Paramètres → Langue** permet de choisir le français ou l’anglais.

Les collecteurs de base fonctionnent sans PawnIO. Les capteurs avancés facultatifs utilisent PawnIO pour lire davantage de données du processeur, de la carte mère et de la mémoire sur le matériel pris en charge. Consulte les [capteurs avancés](doc:monitoring#advanced-sensors) pour la configuration et la compatibilité.

## Pour continuer {#next}

- [Lire les capteurs et les statistiques de session](doc:monitoring#sensor-table).
- [Compiler la bibliothèque et choisir les composants CMake](doc:integration#build).
- [Intégrer le tableau de bord](doc:integration#dashboard).
