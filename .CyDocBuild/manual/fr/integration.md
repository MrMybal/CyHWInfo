---
id: integration
title: Intégrer la bibliothèque C++
description: Compiler, lier et interroger les composants indépendants de CyHWInfo.
section: CyHWInfo
parent: overview
order: 20
sourceRevision: 1
---

## Compiler un composant {#build}

Utilise CMake 3.22 ou supérieur, Git et un compilateur C++23. Pour la configuration Windows validée, utilise Visual Studio 2022 17.10 ou supérieur et un SDK Windows avec les en-têtes WDDM 2.4 et NVMe. CMake récupère la révision épinglée de `lfreist/hwinfo`. Le paramètre `FETCHCONTENT_SOURCE_DIR_LFREIST_HWINFO` permet de fournir une copie locale de cette révision.

Exécute ces commandes depuis le dépôt CyHWInfo pour compiler l’inventaire et les mesures GPU.

```powershell
cmake -S . -B out/gpu "-DCYHWINFO_COMPONENTS=Gpu;GpuTelemetry"
cmake --build out/gpu --config Release
cmake --install out/gpu --config Release --prefix out/install-gpu
```

Ajoute le préfixe d’installation à `CMAKE_PREFIX_PATH` dans le projet qui utilise la bibliothèque.

```cmake
find_package(CyHWInfo CONFIG REQUIRED COMPONENTS Gpu GpuTelemetry)
target_link_libraries(my_app PRIVATE CyHWInfo::Gpu CyHWInfo::GpuTelemetry)
```

Les collecteurs sélectionnables sont `Cpu`, `Gpu`, `GpuTelemetry`, `GpuProcesses`, `Memory`, `Storage`, `StorageHealth`, `Motherboard`, `Battery`, `Network` et le module Windows facultatif `LibreHardwareMonitor`. Le paquet exporte aussi `Core` et `All`. Les mesures et compteurs GPU activent automatiquement `Gpu`. `StorageHealth` active `Storage`.

## Lire les valeurs matérielles {#readings}

Inclus l’en-tête public correspondant sous `<cyhwinfo/>`. Utilise `gpu::enumerate()` pour découvrir les adaptateurs, puis passe leurs identifiants à `gpu::Sampler::open()` depuis `<cyhwinfo/gpu_telemetry.hpp>`. Conserve un collecteur par adaptateur et espace les appels pour les compteurs de débit. Vérifie le `Result` avant d’accéder à sa valeur. Chaque `Reading` fournit une valeur facultative, son état et sa source.

Les identifiants des adaptateurs WDDM sont valables pour le démarrage courant. Le pourcentage de puissance D3DKMT et la puissance en watts fournie par les pilotes constructeur sont deux mesures distinctes. Sous Windows, la santé des disques couvre les périphériques NVMe et SATA directs lorsque le pilote expose l’accès SMART. Actualise l’inventaire après avoir reconnecté un disque.

## Sélectionner les groupes avancés {#advanced-groups}

Lie `CyHWInfo::LibreHardwareMonitor` et appelle `cyhwinfo_deploy_libre_hardware_monitor(my_app)` pour la cible exécutable. La compilation exige le SDK .NET. Le collecteur déployé utilise .NET 8. Consulte les [capteurs avancés](doc:monitoring#advanced-sensors) pour l’accès au pilote.

```cpp
#include <cyhwinfo/sensors.hpp>

auto sampler = cyhwinfo::sensors::Sampler::open({
    .cpu = false, .motherboard = false, .memory = true
});
if (sampler) {
    auto sample = sampler->sample();
    if (sample && sample->memory) {
        const auto& readings = sample->memory->sensors;
        // Afficher les valeurs disponibles dans readings.
    }
}
```

Le relevé indique aussi si PawnIO est installé et accessible. Un groupe est absent si son option est désactivée. Conserve le collecteur entre les appels successifs.

## Intégrer le tableau de bord {#dashboard}

Active `CYHWINFO_BUILD_UI` et fournis `CyUI::Core` ou une copie accessible de CyImgui_Interface. Lie `CyHWInfo::UI`. Avec une archive UI installée, utilise une compilation compatible de CyUI et ImGui. Active `CYHWINFO_BUILD_APP` pour compiler le moniteur Win32/Direct3D 11.

Après avoir créé le contexte ImGui de l’application hôte et initialisé CyUI, appelle `cyhwinfo::ui::register_translations()`. Conserve un `Monitor` et un `Dashboard`. Appelle `dashboard.draw(monitor)` dans la fenêtre hôte, ou utilise `dashboard.draw_panel(Page::sensors, *monitor.snapshot())` pour intégrer uniquement le tableau des capteurs. Le moniteur collecte les données en arrière-plan et publie des instantanés immuables.
