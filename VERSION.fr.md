# CyHWInfo 0.1.0

[English](VERSION.md) · [README](README.fr.md)

## En développement

- Bibliothèque C++23 modulaire avec des cibles CMake pour l’inventaire matériel et les mesures.
- Tableau de bord CyUI et moniteur de capteurs Windows facultatifs.
- Intégration facultative de LibreHardwareMonitor et de PawnIO pour des mesures supplémentaires du processeur, de la carte mère et de la mémoire.
- Relevés AMD Raphael pour les températures par cœur, PPT/TDC/EDC, HTC et PROCHOT, les timings DDR5 actifs, la bande passante DRAM et les résidences des états C sur les tables prises en charge.

## Limites de compatibilité

La validation actuelle couvre Windows x64 et la table SMU `0x00540004` pour les relevés AMD Raphael étendus. Les relevés disponibles varient selon le matériel et le firmware. L’activité WDDM par processus et le SMART NVMe/SATA dépendent de Windows et des pilotes. Les lectures qui utilisent PawnIO exigent aussi un accès à son périphérique sous Windows.
