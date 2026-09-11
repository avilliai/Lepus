# Lepus – Natives, eigenständiges Protokoll-Framework für NTQQ

Lepus ist ein hochmodernes, eigenständig entwickeltes Protokoll-Framework für den Desktop-Client von Tencent NTQQ. Es basiert auf einer reinen **C++20-Architektur** und bietet extrem kurze Latenzzeiten sowie einen minimalen Speicherbedarf. Lepus ist vollständig kompatibel zum Standardprotokoll **OneBot v11** und verfügt darüber hinaus über ein natives, zukunftssicheres Agent-Protokoll für Large Language Models (LLMs).

---

## Architektur & Danksagung (References & Credits)

Während der Analyse und Protokoll-Erforschung hat Lepus wertvolle Erkenntnisse aus bestehenden Open-Source-Projekten bezogen und diese in eine vollständig native C++20-Implementierung überführt:

1. **[LuckyLilliaBot (LLOB)](https://github.com/LLOneBot/LuckyLilliaBot)**:
   - **Referenz**: Electron-Schicht, Node Native Addon-Bridge und standardisierte OneBot-Schnittstellenabstraktion.
2. **[SnowLuma](https://snowluma.github.io/)**:
   - **Referenz**: NTQQ-Protobuf-Definitionen, NTV2RichMedia-Strukturen (OIDB 0x11c4 / 0x11c5 Medien-Vorabprüfung, Highway-Header und commonElem-Format).
3. **[PMHQ](https://github.com/pure-mojo-hook-qq)**:
   - **Referenz**: Mojo-IPC-Mechanismus und Konzept der Steuerungs-Named-Pipes (Control Pipe).

### Technische Unterschiede & Kernvorteile

| Kriterium | LuckyLilliaBot (LLOB) | SnowLuma | Lepus (Dieses Projekt) |
| :--- | :--- | :--- | :--- |
| **Programmiersprache & Laufzeit** | TypeScript + Node.js + Electron Frontend-Injektion | TypeScript / Node.js Laufzeitabhängigkeit | **Reines C++20-Kompilat**, keinerlei externe Laufzeitumgebung |
| **Ressourcenverbrauch** | Hoch (erfordert Electron- & Chromium-Renderkontext) | Moderat (benötigt Node/Bun-Brückendienst) | **Extrem gering** (< 15 MB RAM pro Instanz, Mikrosekunden-Latenz) |
| **Angriffsfläche / Erkennung** | Anfällig für: JS-Patching im Renderer, DOM/UI-Injektion, Electron-Hooking | Anfällig für: Auffällige IPC-Named-Pipes externer Node-Prozesse | **In-Process-Native-Injektion**, 100 % unveränderte Originaldateien, keine Node/V8-Spuren |
| **Signaturprüfung & Risikokontrolle** | Gebunden an den NT-Frontend-Kontext | Abhängig von RPC-Antworten der Protokollschicht | **Wiederverwendung nativer NTQQ-Kernelkanäle**, automatische Übernahme aller offiziellen ECDH/FeKit-Signaturen |
| **Protokollumfang** | OneBot v11 | Internes Format / OneBot-Adapter | **OneBot v11 (HTTP + WebSocket) + Modern Agent Protocol (Tool Calling, CoT-Streaming)** |

---

## Erkennungspunkte & Sicherheitsmechanismen

Aktuelle Sicherheitsprüfungen des offiziellen NTQQ-Clients und die Gegenmaßnahmen von Lepus:

1. **Dateisystem- und Hash-Prüfung**:
   - *Risiko*: Modifikationen an 
esources\app oder das Laden von Frameworks wie LiteLoaderQQNT werden durch Hash-Scans erkannt.
   - *Lepus*: Verändert **keine einzige Datei** im QQ-Installationsverzeichnis. Digitale Signaturen und Dateistrukturen bleiben unangetastet.
2. **Node.js- / Electron-Laufzeitinspektion**:
   - *Risiko*: V8-Prototypenketten und globale JavaScript-Objekte werden auf Injektionen analysiert.
   - *Lepus*: Läuft vollständig im nativen C++-Adressraum und greift nicht auf die V8-Engine zu.
3. **SSO-Signaturvalidierung (Dverify / Sign-Server)**:
   - *Risiko*: Emulierte Clients scheitern häufig an dynamischen ECDH/Dverify-Tokens, was zu Sperren führt.
   - *Lepus*: Verwendet die bestehende interne Sitzung der offiziellen Desktop-App für MessageSvc.PbSendMsg und OidbSvcTrpcTcp. Alle Pakete tragen valide, offizielle Signaturen.

---

## Richtlinien für Git & Veröffentlichung

Bei der Veröffentlichung im öffentlichen Repository werden Artefakte durch .gitignore gefiltert:

### Im Repository enthalten (Quellcode):
- include/: Sämtliche C++-Header (Netzwerkserver, Protokoll-Parser, Hook-Abstraktionen, IPC)
- src/: Einstiegspunkte (dllmain.cpp, launcher_main.cpp)
- Makefile: Build-Skript für MinGW-w64 (GCC 13+)
- config/: Konfigurationsvorlage (config/config.json)
- docs/: Architektur- und Entwurfsspezifikationen
- 	hird_party/: Quelltextabhängigkeiten (httplib/, 
lohmann/, minhook/)
- README.md, .gitignore

### Ausgeschlossen (durch .gitignore geschützt):
- in/ (Kompilierte Binärdateien .exe, .dll, Build-Artefakte)
- *.log (Lokale Protokolle mit sensiblen Metadaten)
- Temporäre Testskripte und Reverse-Engineering-Dumps

---

## Bereitstellung auf anderen Geräten (Stand-alone / Ohne Build-Tools)

Für den reinen Betrieb auf einem Zielsystem ohne Entwicklungsumgebung:

### Empfohlene Verzeichnisstruktur:
`	ext
📁 Lepus/
├── 📁 config/
│   └── 📄 config.json           (Port- & WebSocket-Konfiguration)
├── 📁 bin/
│   ├── 📄 lepus_launcher.exe    (Startprogramm & DLL-Injektor)
│   └── 📄 lepus_core.dll        (Haupt-Engine)
└── 📁 third_party/
    └── 📄 lepus_hook.dll        (Hook-Treiber)
`
*(Hinweis: lepus_launcher.exe unterstützt auch eine flache Ordnerstruktur, in der alle DLLs und die Konfigurationsdatei im selben Ordner liegen).*

### Voraussetzungen:
- Installierter und angemeldeter offizieller **NTQQ 64-Bit-Client**.
- Microsoft Visual C++ 2015–2022 Redistributable (x64).

---

## Schnellstart

1. **Offiziellen NTQQ-Client starten**:
   Starten Sie den QQ-Desktop-Client und melden Sie sich regulär an.

2. **Lepus mit Administratorrechten starten**:
   `powershell
   .\bin\lepus_launcher.exe
   `
   Nach erfolgreicher Injektion stehen folgende Endpunkte zur Verfügung:
   - **OneBot v11 HTTP API**: http://127.0.0.1:3000
   - **OneBot v11 WebSocket**: ws://127.0.0.1:3001

3. **Bot-Framework anbinden**:
   Verbinden Sie beliebige OneBot v11-kompatible Bots (z. B. NoneBot2, Koishi, AstrBot, Eridanus) mit dem WebSocket-Port 3001.
