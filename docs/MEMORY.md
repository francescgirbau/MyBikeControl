# Memòria del projecte i de la sessió

> Context per reprendre el projecte sense haver de reconstruir les decisions.
> Última actualització: **2026-11-01**.
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

- **Versió:** 0.1.0 (esquelet + Fase 1)
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

### Button IDs previstos

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
| 2. Descodificació del controlador Zwift | ⏳ següent |
| 3. Pont i mapatge configurable | ⏳ |
| 4. Integració i latència | ⏳ |
| 5. Robustesa | ⏳ |
| 6. Empaquetat Windows | ⏳ |

`serve` ja anuncia el dispositiu per mDNS i accepta connexions TCP. El que encara
no hi és és el descodificador que converteix les notificacions BLE del manillar en
botons; per això `--demo` és la manera de provar el transport avui.

---

## 9. Registre de la sessió (2026-11-01)

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

### Decisions d'aquesta sessió

- No controlar mai el KICKR; només pass-through de `Shift Up` / `Shift Down`.
- Mantenir Docker per a CI/demo, no com a runtime.
- Mantenir `windows-2022` al CI mentre `windows-latest` no tingui el generador
  adequat.
- Fer servir PRs i squash encara que l'administrador pugui tècnicament saltar-se
  la protecció (no ho farà: `enforce_admins` ho impedeix).

---

## 10. Properes passes

1. **Capturar payloads reals** amb `mybc inspect <index> --notify` per a la variant
   de manillar disponible (Ride / Play / Click / firmware).
2. Implementar el **descodificador** de la Fase 2:
   - detectar la variant per nom/servei/firmware;
   - debounce per contacte (press + release);
   - tests unitaris amb payloads reals.
3. Connectar el descodificador al servidor OBC (Fase 3):
   - `config.json` amb mapatge `payload → button ID`;
   - enviar `0x01` només en canvi d'estat;
   - filtrar per App Info.
4. Provar amb **hardware Windows real** i MyWhoosh:
   - descoberta mDNS;
   - latència de puny a canvi de marxa;
   - firewall i xarxa Private.
5. Robustesa (Fase 5): reconnexió BLE, diversos clients TCP, Device Status,
   tancament net amb `SIGINT`/`SIGTERM`.
6. Empaquetat Windows (Fase 6): runtime MSVC o `/MT`, firewall, opcionalment
   instal·lador i signatura.

---

## 11. Limitacions i riscos actuals

- **No s'ha provat amb hardware real** en aquest entorn: BLE, mDNS i MyWhoosh
  només es poden validar en un Windows amb Bluetooth i el manillar.
- El CI valida compilació i TCP, però **no** BLE ni descoberta mDNS real.
- `serve` sense `--demo` encara **no** reenvia botons reals: falta el descodificador.
- El protocol del controlador pot variar segons firmware; cal capturar payloads.
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
