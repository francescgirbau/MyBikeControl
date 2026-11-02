# Memòria del projecte i de la sessió

> Context per reprendre el projecte sense haver de reconstruir les decisions.
> Última actualització: **2026-11-02**.
> Documents principals: [`README.md`](../README.md) i [`PLAN.md`](PLAN.md).

---

## 1. Resum executiu

**MyBikeControl** és un pont local i natiu per a **Windows 10 1709+ / Windows 11 x64**
que llegeix els botons del manillar **Zwift Ride / Play / Click** per BLE i els
publica com a dispositiu **OpenBikeControl (OBC)** per mDNS + TCP. **MyWhoosh** els
rep i aplica el canvi de marxa amb **MyShift**.

```
Zwift Ride/Play --BLE--> MyBikeControl --OpenBikeControl--> MyWhoosh --FTMS--> KICKR
```

El **Wahoo KICKR Core no es toca**: el continua controlant MyWhoosh. Enviem només
`Shift Up` / `Shift Down` per evitar que dues centrals escriguin al mateix
*Fitness Machine Control Point*.

- **Versió:** 0.2.0 (Fase 1 + descodificador Ride + integració al `serve`)
- **Plataforma objectiu:** Windows 10 1709+ / Windows 11, x64
- **Llenguatge:** C++17
- **Build:** CMake ≥ 3.21 + MSVC 2022 (Visual Studio 17 2022)

---

## 2. Abast i no-objectius

### Dins de l'abast

1. Connectar per BLE al manillar i descodificar les pulsacions.
2. Traduir-les a *button IDs* estàndard d'OpenBikeControl.
3. Publicar-se com a dispositiu OBC per mDNS + TCP.
4. Deixar que MyWhoosh apliqui la marxa i mani el KICKR com sempre.

### Fora de l'abast (MVP)

- Control directe del KICKR o escriptura al *Fitness Machine Control Point*.
- Interfície gràfica, núvol, comptes, telemetria.
- Suport d'altres rodets o altres plataformes com a runtime principal.

---

## 3. Arquitectura

```
┌──────────────────────┐   BLE (central)   ┌──────────────────────────┐
│ Zwift Ride / Play /  │ ────────────────> │  MyBikeControl (C++)     │
│ Click                │  notificacions    │                          │
└──────────────────────┘                   │  1. descodifica botons   │
                                           │  2. tradueix a IDs OBC   │
                                           │  3. publica OBC          │
                                           └────────────┬─────────────┘
                                                        │ mDNS + TCP
                                                        v
                                           ┌──────────────────────────┐
                                           │  MyWhoosh                │
                                           │  MyShift + control FTMS  │
                                           └────────────┬─────────────┘
                                                        │ BLE (FTMS)
                                                        v
                                           ┌──────────────────────────┐
                                           │  Wahoo KICKR Core        │
                                           └──────────────────────────┘
```

Responsabilitats:

- **MyBikeControl:** llegir el controlador, debounce, mapatge, publicar OBC, reconnexió.
- **MyWhoosh:** descobrir el dispositiu OBC, aplicar la marxa, calcular i enviar la resistència.
- **KICKR:** obeir MyWhoosh. El bridge no hi parla mai.

---

## 4. Decisions tècniques clau

| Tema | Decisió | Motiu |
| --- | --- | --- |
| Plataforma | Windows 10 1709+ / Windows 11 x64 | Objectiu real; macOS queda com a secundari |
| BLE central | **SimpleBLE 1.3.0** amb backend WinRT | Multiplataforma, API simple, backend natiu a Windows |
| Transport | **mDNS + TCP** (Winsock a Windows) | C++ pur, sense APIs de perifèric, suportat per MyWhoosh |
| mDNS Windows | **DNS-SD natiu** (`DnsServiceRegister`) | Zero dependències externes; Windows 10 1709+ |
| mDNS macOS | `dns_sd` | Plataforma secundària |
| mDNS Linux | Stub | CI i compilació, no runtime |
| Sockets | Winsock a Windows, POSIX a Linux/macOS | Un sol codi de servidor amb abstracció mínima |
| Control del KICKR | **No tocar-lo** | MyWhoosh ja hi escriu; evitar *last-writer-wins* |
| Concurrència | `std::thread`, `std::mutex`, `std::atomic` | Un fil per client TCP + un fil d'acceptació |
| Configuració | JSON (Fase 3) | Mapatge de botons sense recompilar |
| UI | CLI | El MVP no necessita interfície |
| Docker | Només CI/demo | No té Bluetooth de l'amfitrió ni mDNS real |

### Per què no controlem el KICKR

MyWhoosh ja envia ordres de resistència segons el pendent, el mode ERG i la marxa
virtual. Si el bridge també hi escriu, les ordres es trepitgen i la resistència fa
salts. La solució és **no ser un actor més**: ser només una font d'esdeveniments de
canvi de marxa.

---

## 5. Detalls específics de Windows

### Build

- Generador: `"Visual Studio 17 2022"` amb `-A x64`.
- CMake ≥ 3.21.
- Amb **Visual Studio 2026** cal CMake ≥ 4.2 i el generador `"Visual Studio 18 2026"`.
- Flags/llibres afegits a `CMakeLists.txt`:
  - `ws2_32` (Winsock)
  - `dnsapi` (DNS-SD)
  - `_WIN32_WINNT=0x0A00`
  - `WIN32_LEAN_AND_MEAN`
  - `NOMINMAX`
  - MSVC `/utf-8`

### Sockets

- `include/mybc/net.h` i `src/net.cpp` abstractionen `Socket`, inicialització i
  neteja de Winsock (`WSAStartup` / `WSACleanup`) i errors.
- `src/obc/server.cpp` fa servir Winsock a Windows i POSIX a Linux/macOS.
- `src/main.cpp` inicialitza i neteja Winsock i configura la consola en UTF-8.

### mDNS amb DNS-SD

- `src/obc/mdns.cpp` registra el servei amb `DnsServiceConstructInstance` /
  `DnsServiceRegister` / `DnsServiceDeRegister`.
- Nom d'instància FQDN: `Name._openbikecontrol._tcp.local`.
- Host: `<nom-equip>.local`.
- `wPort` va en **host byte order**.
- `Version = DNS_QUERY_REQUEST_VERSION1`.
- `unicastEnabled = FALSE`.
- `DnsServiceRegister` pot retornar **`DNS_REQUEST_PENDING`** en cas d'èxit.
- Fallback per a MinGW: `#define DNS_REQUEST_PENDING 9506L`.

### Firewall

- TCP del bridge: **8080** (configurable).
- mDNS: **UDP 5353** multicast.
- Perfil de xarxa de casa: **Private**.
- Afegir `mybc.exe` a les excepcions de Windows Defender Firewall (Private).

---

## 6. Contracte OpenBikeControl implementat

### Descoberta mDNS

- Tipus: `_openbikecontrol._tcp.local.`
- Nom: `<Device Name>._openbikecontrol._tcp.local.`
- TXT: `version=1`, `id=<únic>`, `name=<nom>`,
  `service-uuids=d273f680-d548-419d-b9d1-fa0472345229`,
  `manufacturer=<nom>`, `model=<model>`
- TCP per defecte: **8080**

### Missatges

| Tipus | Direcció | Format |
| --- | --- | --- |
| `0x01` Button State | bridge → app | `[0x01, id, state, id, state, ...]` |
| `0x02` Device Status | bridge → app | `[0x02, battery, connected]` (cada 30 s) |
| `0x03` Haptic Feedback | app → bridge | `[0x03, pattern, duration, intensity]` |
| `0x04` App Info | app → bridge | `[0x04, ver, idLen, id..., verLen, ver..., count, ids...]` |

**Framing TCP:** la spec no defineix prefix de longitud per als missatges `0x01`.
Convenció adoptada: **un `send()` per missatge** amb `TCP_NODELAY`. El parser
d'entrada és incremental i tolerant perquè `0x03` i `0x04` sí que es poden
reconstruir.

### Button IDs implementats

| ID | Acció | Origen previst |
| --- | --- | --- |
| `0x01` | Shift Up | Botó `+` / costat dret |
| `0x02` | Shift Down | Botó `-` / costat esquerre |
| `0x10`–`0x13` | Navegació | Botons de direcció |
| `0x14` | Select/Confirm | Botó central |
| `0x15` | Back/Cancel | Botó de tornar |
| `0x16` | Menu | Botó de menú |
| `0x17` | Home | Botó de casa |
| `0x18`/`0x19` | Steer Left/Right | Botons de direcció del manillar |
| `0x20` | Emote | Botó social |
| `0x30`/`0x31` | +/- dificultat | Mapatge opcional |
| `0x40` | Camera View | Mapatge opcional |
| `0x44` | HUD Toggle | Mapatge opcional |

Els botons no suportats per l'app s'ignoren gràcies al missatge `0x04` (App Info),
que arriba just després de connectar.

### Zwift Ride: protocol i mapatge

- **Servei:** `0000fc82-0000-1000-8000-00805f9b34fb`.
- **Notificacions de botons:** `00000002-19ca-4651-86e5-fa29dcdd09d1`.
- **Escriptura del handshake:** `00000003-19ca-4651-86e5-fa29dcdd09d1`.
- **Indicacions del handshake:** `00000004-19ca-4651-86e5-fa29dcdd09d1`.
- **Número de sèrie:** `00002a25-0000-1000-8000-00805f9b34fb`
  (servei Device Information `0000180a-...`).
- **Handshake:** subscriure a `00000002` (notify) i `00000004` (indicate), esperar
  ~500 ms i escriure ASCII `RideOn` (`52 69 64 65 4f 6e`) a `00000003` sense
  resposta.
- **Opcode de botons:** `0x23` (`RideKeyPadStatus`); keep-alive `0x15`; bateria
  `0x19`; missatge de vendor `0x2a`.
- **Protobuf:** camp 1 = `buttonMap` (varint, **actiu baix**: bit a 0 = premut);
  camp 3 = `analogPaddles` repetit (`location` camp 1, `value` camp 2).
- **Màscares Ride** (les de `zwift_ride.dart`, no les del protobuf generat):
  - `0x00001` LEFT, `0x00002` UP, `0x00004` RIGHT, `0x00008` DOWN;
  - `0x00010` A, `0x00020` B, `0x00040` Y, `0x00080` Z;
  - `0x00100` SHIFT_UP_L, `0x00200` SHIFT_DN_L, `0x00400` POWERUP_L,
    `0x00800` ONOFF_L;
  - `0x01000` SHIFT_UP_R, `0x02000` SHIFT_DN_R, `0x04000` POWERUP_R,
    `0x08000` ONOFF_R.
- **Mapatge OBC:**
  - LEFT/RIGHT → `Steer Left`/`Steer Right`;
  - UP/DOWN → `Up`/`Down`;
  - A/B/Y/Z → `Select`/`Back`/`Menu`/`Emote`;
  - tots els controls d'esquerra (`SHIFT_UP_L`, `SHIFT_DN_L`, `POWERUP_L`) i la
    palanca analògica `location 0` → `Shift Down`;
  - tots els controls de dreta (`SHIFT_UP_R`, `SHIFT_DN_R`, `POWERUP_R`) i la
    palanca analògica `location 1` → `Shift Up`;
  - ONOFF_L/R → `HUD Toggle`.
- **Palanca analògica:** `location 0` = esquerra, `location 1` = dreta; valor
  absolut; pressió ≥ 25, alliberament ≤ 20 (histèresi). El dispositiu envia les
  quatre ubicacions a cada notificació; les que falten es tracten com a 0.
- **Meitats del Ride:** la meitat esquerra (sèrie `08-...`) actua com a mestre i
  reporta navegació, controls esquerres i totes les palanques; la dreta
  (`07-...`) reporta els seus controls i la palanca dreta. El bridge connecta a
  totes dues i **fusiona** els estats OBC amb unió, de manera que un botó reportat
  per les dues només genera una parella press/release.
- **Captures de referència:** esquerra `08-4222A1C4D938` (`C2:22:A1:C4:D9:38`),
  dreta `07-9031A296E373` (`D0:31:A2:96:E3:73`), firmware `1.2.0`, hardware `B.0`.
  Els controladors s'adormen de pressa: cal prémer un botó per despertar-los i
  escanejar al mateix procés.

---

## 7. Repositori, CI i política de merge

### Repositori

- **Públic:** `francescgirbau/MyBikeControl`.
- Branca per defecte: **`main`**.
- Només **squash and merge**:
  - `allow_squash_merge: true`
  - `allow_merge_commit: false`
  - `allow_rebase_merge: false`
- Esborrat automàtic de branques en fusionar: **desactivat** (es fa manualment).

### Protecció de `main`

- Checks obligatoris:
  - `Windows (MSVC + SimpleBLE)`
  - `Linux (codec and TCP server)`
- `strict: true` → la branca ha d'estar actualitzada amb `main`.
- `enforce_admins: true` → ni l'administrador pot saltar-se els checks.
- `required_linear_history: true`.
- Force push i esborrat de `main`: prohibits.

> Si es canvien els noms dels jobs de `.github/workflows/ci.yml`, cal actualitzar
> els checks obligatoris a la protecció de `main`; si no, els PRs quedaran bloquejats.

### CI

- `Windows (MSVC + SimpleBLE)`
  - Runner: **`windows-2022`** (pinned).
  - Motiu: `windows-latest` és ara Windows 2025 / VS 2026 amb CMake 4.1.2, que no
    té el generador de VS 2026.
  - Configura i compila amb SimpleBLE, executa `ctest` i fa un smoke test TCP:
    `mybc serve --demo --no-mdns --port 18080` i el client llegeix `02 ff 01`.
- `Linux (codec and TCP server)`
  - `ubuntu-latest`, `MYBC_WITH_BLE=OFF`, build i `ctest`.
- El workflow s'executa en `push` i `pull_request`.
- Les anotacions de Node.js 20 de `actions/checkout@v4` són avisos de deprecació,
  no fallades.

---

## 8. Estat actual

| Fase | Estat |
| --- | --- |
| 1. Descoberta BLE (`scan`, `inspect --notify`) | ✅ implementat |
| Transport OBC (`serve`, mDNS/TCP, `--demo`) | ✅ implementat |
| 2. Descodificació del controlador Zwift Ride | ✅ implementat |
| 3. Pont i mapatge | ✅ integrat al `serve` (mapatge fix; JSON pendent) |
| 4. Integració i latència | ⏳ |
| 5. Robustesa | ⏳ |
| 6. Empaquetat Windows | ⏳ |

`serve` sense `--demo` ja busca el Zwift Ride per BLE, envia el handshake `RideOn`,
descodifica les notificacions i reenvia els botons a MyWhoosh. Les dues meitats del
Ride es fusionen per evitar esdeveniments duplicats. El mapatge és fix per al Ride;
Zwift Play/Click i el `config.json` queden per a versions posteriors.

---

## 9. Registre de les sessions (2026-11-01/02)

### Implementació

- Suport natiu de Windows complet:
  - abstracció de sockets (`include/mybc/net.h`, `src/net.cpp`);
  - servidor TCP amb Winsock (`src/obc/server.cpp`);
  - mDNS amb DNS-SD natiu (`src/obc/mdns.cpp`);
  - flags i llibreries de Windows a `CMakeLists.txt`;
  - inicialització/neteja de Winsock i consola UTF-8 a `src/main.cpp`.
- README i `docs/PLAN.md` reorientats a Windows.
- CI de Windows i Linux en verd.
- Verificació de compilació creuada MinGW-w64 (`MYBC_WITH_BLE=OFF`).
- Build Docker Linux (`MYBC_WITH_BLE=OFF`) + `ctest` correctes.

### Integració

- PR #6 (`feat/windows` → `main`) reorientat a `main` i fusionat amb **squash**.
- Commit d'integració: **`ec411e8`**
  (`feat: suport natiu per a Windows (Winsock + DNS-SD + WinRT) (#6)`).
- CI de `main` en verd:
  - Windows: 4m31s.
  - Linux: 21s.

### Configuració del repositori

- Repositori fet **públic** per habilitar la protecció de branca al pla Free.
- Abans de publicar-lo es va comprovar que no hi hagués secrets ni fitxers
  sensibles versionats.
- Merge configurat a **squash-only**.
- Protecció de `main` aplicada amb els checks obligatoris, `strict`,
  `enforce_admins`, historial lineal i sense force push/esborrat.
- Branques antigues esborrades (locals i remotes):
  `build/docker`, `chore/bootstrap`, `docs/plan`, `feat/ble-scanner`,
  `feat/obc-server`, `feat/windows`.
- Estat final de branques: només `main`.

### Sessió 2026-11-02: descodificador i pont Ride

- Captura BLE real de les dues meitats del Ride amb `bleak` 3.0.2 (WinRT).
- Identificat el protocol: opcode `0x23`, protobuf amb `buttonMap` actiu baix i
  `analogPaddles`; handshake `RideOn`; keep-alive `0x15`; bateria `0x19`.
- Implementat `RideDecoder` (`include/mybc/ble/ride_decoder.h`,
  `src/ble/ride_decoder.cpp`):
  - parser protobuf sense dependències;
  - mapatge de màscares Ride a IDs OBC;
  - histèresi de les palanques analògiques;
  - detecció de flancs per generar exactament un press i un release.
- Implementat `RideBridge` (`include/mybc/ble/ride_bridge.h`,
  `src/ble/ride_bridge.cpp`):
  - scan de dispositius Zwift Ride;
  - connexió a totes les meitats, lectura de sèrie, subscripcions i `RideOn`;
  - fusió d'estats per adreça i reconnexió automàtica;
  - actualització de bateria.
- Integrat al `serve`:
  - opció `--controller` per filtrar nom/adreça;
  - filtratge per App Info (`Server::appSupports`);
  - enviament de tots els canvis simultanis en un sol missatge `0x01`;
  - `--demo` continua funcionant sense BLE.
- Tests:
  - `tests/test_ride_decoder.cpp` amb payloads reals de released, Shift Up/Down,
    palanques, navegació, A/B/Y/Z, ON/OFF, keep-alive, missatges malformats i reset.
  - Replay de les 966 notificacions de la captura: 22 press i 22 release, cap event
    espuri, estat final buit.
- Build Linux (`ubuntu:24.04`, `MYBC_WITH_BLE=OFF`): 2/2 tests correctes.
- Branca BLE compilada i enllaçada al CI de Windows amb SimpleBLE/WinRT; els
  tests també passen allà.
- PR #8 (`feat/ride-decoder` → `main`) fusionat amb **squash**: `eddc6aa`.
- CI de `main` en verd: Windows 4m26s, Linux 22s.
- **Pendent:** prova amb hardware real (MyWhoosh + manillar) i mesura de latència.

### Decisions d'aquesta sessió

- No controlar mai el KICKR; només pass-through de `Shift Up` / `Shift Down`.
- Mantenir Docker per a CI/demo, no com a runtime.
- Mantenir `windows-2022` al CI mentre `windows-latest` no tingui el generador
  adequat.
- Fer servir PRs i squash encara que l'administrador pugui tècnicament saltar-se
  la protecció (no ho farà: `enforce_admins` ho impedeix).

---

## 10. Properes passes

1. **Provar amb hardware Windows real** i MyWhoosh:
   - compilar amb MSVC + SimpleBLE/WinRT;
   - descoberta mDNS;
   - prémer els botons del Ride i comprovar `Shift Up`/`Shift Down`;
   - mesurar latència de puny a canvi de marxa;
   - firewall i xarxa Private.
2. **Afegir suport per Zwift Play / Click**:
   - detectar la variant per nom/servei/firmware;
   - reutilitzar el parser i el mapatge OBC;
   - capturar payloads amb `inspect --notify`.
3. **Configuració** (Fase 3 completa):
   - `config.json` amb mapatge `payload → button ID` i opcions de debounce;
   - permetre desactivar botons no relacionats amb el canvi de marxa.
4. Robustesa (Fase 5): backoff exponencial, diversos clients TCP, Device Status,
   tancament net amb `SIGINT`/`SIGTERM`.
5. Empaquetat Windows (Fase 6): runtime MSVC o `/MT`, firewall, opcionalment
   instal·lador i signatura.

---

## 11. Limitacions i riscos actuals

- **No s'ha provat amb hardware real** en aquest entorn: BLE, mDNS i MyWhoosh
  només es poden validar en un Windows amb Bluetooth i el manillar.
- El CI valida compilació i TCP, però **no** BLE ni descoberta mDNS real.
- `serve` sense `--demo` ja reenvia botons del **Zwift Ride**; Zwift Play/Click
  encara no estan implementats.
- El protocol del controlador pot variar segons firmware; cal capturar payloads.
- La branca BLE s'ha compilat i enllaçat al CI de Windows amb SimpleBLE/WinRT i
  els tests passen; encara no s'ha provat amb el manillar físic ni MyWhoosh.
- El framing TCP de `0x01` no té length prefix; es manté un `send()` per missatge.
- SimpleBLE 1.3.0 és BSL 1.1 per a ús no comercial; si el projecte es comercialitza,
  cal una llicència alternativa.
- La protecció de `main` depèn dels noms exactes dels jobs de CI.
- El repositori és públic: qualsevol canvi futur no ha d'incloure secrets.

---

## 12. Comandes útils

### Windows (PowerShell)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

.\build\Release\mybc.exe scan --seconds 10
.\build\Release\mybc.exe inspect 0 --notify --seconds 30
.\build\Release\mybc.exe serve --demo
.\build\Release\mybc.exe serve --name "MyBikeControl"
.\build\Release\mybc.exe serve --name "MyBikeControl" --controller "Zwift Ride"
```

### Linux / CI

```bash
cmake -S . -B build -DMYBC_WITH_BLE=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

### Docker (només demo/CI)

```bash
docker build -t mybc .
docker run --rm -p 8080:8080 mybc
docker compose run --rm test
```

### Git / GitHub

```bash
git switch main
git pull --ff-only
git switch -c feat/descripcio
# ... canvis ...
git push -u origin feat/descripcio
gh pr create --base main --fill
gh pr merge --squash
```

---

## 13. Llicències

- **OpenBikeControl protocol:** MIT.
- **SimpleBLE 1.3.0:** Business Source License 1.1, ús no comercial permès.
- **BikeControl:** *Non-Commercial*; es fa servir només com a referència del
  protocol del controlador, sense copiar codi.

El projecte és local: no envia dades enlloc, no fa telemetria i no interacciona
amb els servidors de Zwift.
