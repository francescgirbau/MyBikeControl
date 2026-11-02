# MyBikeControl

Pont local i privat entre el manillar **Zwift Ride / Play / Click** i **MyWhoosh**, en C++.

El bridge llegeix els botons del manillar per BLE i els publica com a dispositiu
**OpenBikeControl** (mDNS + TCP). MyWhoosh els rep i aplica el canvi de marxa amb
**MyShift**. El **Wahoo KICKR Core** no es toca: el continua controlant MyWhoosh.

```
Zwift Ride/Play --BLE--> MyBikeControl --OpenBikeControl--> MyWhoosh --FTMS--> KICKR
```

> Per què no controlem el KICKR directament: MyWhoosh ja hi escriu la resistència.
> Dues centrals al mateix *Fitness Machine Control Point* es trepitgen i la resistència
> fa salts. Enviant només `Shift Up`/`Shift Down` no hi ha conflicte.

**Plataforma objectiu: Windows 10/11 x64.** Pla complet i decisions:
[`docs/PLAN.md`](docs/PLAN.md).

---

## Estat

| Fase | Estat |
| --- | --- |
| 1. Descoberta BLE (`scan`, `inspect --notify`) | ✅ implementat |
| Transport OpenBikeControl (`serve`, mDNS/TCP, mode `--demo`) | ✅ implementat |
| 2. Descodificació del controlador Zwift Ride | ✅ implementat |
| 3. Pont i mapatge | ✅ integrat al `serve` (mapatge fix; JSON pendent) |
| 4. Integració i latència | ⏳ |
| 5. Robustesa | ⏳ |
| 6. Empaquetat Windows | ⏳ |

> `serve` sense `--demo` ja busca el **Zwift Ride** per BLE, envia el handshake
> `RideOn`, descodifica les notificacions i reenvia els botons a MyWhoosh. El
> mapatge és fix per al Ride; Zwift Play/Click i el `config.json` queden per a
> versions posteriors. `--demo` continua servint per provar el transport sense
> manillar.

---

## Requisits

- **Windows 10 versió 1709 (build 16299) o posterior**, o **Windows 11**, x64.
  L'API de mDNS (`DnsServiceRegister`) necessita Windows 10 1709+.
- **Visual Studio 2022** amb la càrrega *Desktop development with C++*
  (MSVC v143 i Windows 10/11 SDK).
- **CMake ≥ 3.21** (el pots instal·lar amb el Visual Studio Installer o des de cmake.org).
- **Git**.
- Adaptador Bluetooth 4.0+ (el del portàtil o un dongle USB).
- Connexió a internet la primera vegada (CMake baixa SimpleBLE 1.3.0).

---

## Compilar

Des de PowerShell, a l'arrel del repo:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

El binari queda a `build\Release\mybc.exe`.

> Si fas servir **Visual Studio 2026**, cal **CMake ≥ 4.2** i el generador
> `"Visual Studio 18 2026"` en lloc de `"Visual Studio 17 2022"`. Amb Visual
> Studio 2022 n'hi ha prou amb CMake ≥ 3.21.

Si no vols BLE (per exemple, per compilar només el transport):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMYBC_WITH_BLE=OFF
cmake --build build --config Release
```

---

## Ús

### 1. Descobrir el manillar

```powershell
.\build\Release\mybc.exe scan --seconds 10
```

El manillar ha d'estar despert i **no connectat a Zwift**. Prem un botó per despertar-lo.

### 2. Inspeccionar serveis i capturar botons

```powershell
.\build\Release\mybc.exe inspect 0 --notify --seconds 30
```

Mostra l'arbre GATT i hexdumpeja cada notificació. Prem els botons durant la captura:
és útil per diagnosticar o per afegir suport a variants noves. El descodificador del
**Zwift Ride** ja està implementat i provat amb payloads reals.

### 3. Provar el pont sense manillar

```powershell
.\build\Release\mybc.exe serve --demo
```

Publica el dispositiu per mDNS i alterna `Shift Up`/`Shift Down` cada 3 segons.
És la manera més ràpida de validar la integració amb MyWhoosh.

### 4. Executar el pont

```powershell
.\build\Release\mybc.exe serve --name "MyBikeControl"
```

En arrencar, `serve` busca el Zwift Ride per BLE. Prem un botó del manillar per
despertar-lo; el pont es connecta a les dues meitats, envia el handshake `RideOn`
i comença a reenviar els botons. Si el manillar ja està connectat a Zwift, cal
desconnectar-lo d'allà primer.

Opcions:

| Opció | Descripció |
| --- | --- |
| `--port N` | Port TCP (per defecte 8080; `0` = aleatori) |
| `--name S` | Nom visible a MyWhoosh |
| `--id S` | Identificador únic del TXT mDNS |
| `--controller S` | Filtre de nom/adreça BLE (per defecte `Zwift Ride`) |
| `--no-mdns` | No anunciar per mDNS (proves manuals) |
| `--demo` | Generar canvis de marxa sintètics |
| `--verbose` | Logs de debug |

### Mapatge del Zwift Ride

- Els controls de canvi del costat **esquerre** i la palanca analògica esquerra
  envien `Shift Down` (`0x02`).
- Els controls del costat **dret** i la palanca analògica dreta envien
  `Shift Up` (`0x01`).
- Les dues meitats del Ride es fusionen perquè un botó reportat per totes dues
  generi exactament una parella press/release.
- Els botons de navegació, A/B/Y/Z i ON/OFF també es tradueixen a IDs OBC; si
  l'app no els anuncia a l'App Info, s'ignoren.

---

## Configurar MyWhoosh

1. Obre **Connection**.
2. Toca la icona **OpenBikeControl** a dalt a la dreta.
3. Confirma el popup.
4. Comença a pedalar: els botons de canvi han de moure la marxa de MyShift.

El KICKR segueix emparellat a MyWhoosh com sempre. No el connectis al bridge.

---

## Permisos i firewall de Windows

La primera vegada que arrenquis `mybc.exe`, Windows Defender Firewall et pot demanar
permís:

- **Xarxa privada** — per rebre connexions TCP de MyWhoosh i per anunciar-te per mDNS.
- **Xarxa pública** — no cal; deixa-ho desactivat si només jugues a casa.

Si el port 8080 o l'mDNS no funcionen:

1. Ves a *Windows Security → Firewall & network protection → Allow an app through firewall*.
2. Afegeix `mybc.exe` i marca **Private**.
3. Comprova que la xarxa de casa està marcada com a **Private**
   (*Settings → Network & internet → Wi-Fi → Network profile type*).
4. L'mDNS fa servir **UDP 5353** (multicast); el TCP del bridge, **8080**.

No cal cap permís especial de Bluetooth: SimpleBLE fa servir WinRT i el Windows
Bluetooth stack.

---

## Resolució de problemes

| Símptoma | Causa probable | Solució |
| --- | --- | --- |
| `No Bluetooth adapter found` | Bluetooth apagat o adaptador no disponible | Activa'l a *Settings → Bluetooth & devices* |
| El manillar no apareix a `scan` | Està connectat a Zwift o adormit | Desconnecta'l de Zwift i prem un botó |
| `serve` troba el Ride però no arriben botons | El handshake no s'ha completat o el firmware és diferent | Comprova el log `RideOn handshake sent`; captura amb `inspect --notify` |
| El Ride no canvia la marxa | MyShift desactivat, app no connectada o botó no suportat | Revisa `app connected` i l'App Info; prova `serve --demo` |
| `DnsServiceRegister failed with code …` | Windows massa antic, DNS Client aturat o xarxa bloquejada | Windows 10 1709+; revisa el servei *DNS Client* i el firewall |
| MyWhoosh no veu el bridge | mDNS bloquejat o xarxa diferent | Mateixa xarxa, perfil **Private**, firewall i UDP 5353 |
| `bind() failed on port 8080` | Port ocupat | `--port 0` o un altre port |
| La marxa no canvia | MyShift desactivat o app no connectada | Comprova el log `app connected` i l'estat de MyShift |
| La resistència fa salts | Algú està controlant el KICKR directament | Desconnecta el bridge del KICKR; només MyWhoosh hi ha de parlar |

Per veure què passa amb mDNS, arrenca amb `--verbose` i comprova el log
`mDNS advertising as "…"`.

---

## Docker (només CI i demo)

La imatge de Docker **no és el runtime de Windows**. Docker Desktop corre els
contenidors en una VM Linux sense el Bluetooth de l'amfitrió, i el mDNS no travessa
la VM. Serveix per compilar, passar tests i provar el transport TCP en mode demo:

```bash
docker build -t mybc .
docker run --rm -p 8080:8080 mybc
# equivalent:
docker compose up serve-demo
```

El contenidor arrenca `mybc serve --demo --no-mdns` i escolta a `0.0.0.0:8080`.
Com que no anuncia per mDNS, MyWhoosh no el descobrirà sol: és per provar el
transport amb qualsevol client TCP.

CI i tests:

```bash
docker compose run --rm test          # ctest amb MYBC_WITH_BLE=OFF
docker compose build ble-compile      # compila amb SimpleBLE (MYBC_WITH_BLE=ON)
docker compose run --rm version       # mybc version
```

Per a l'ús real amb MyWhoosh, executa el binari natiu de Windows.

---

## Estructura

```
CMakeLists.txt
Dockerfile
compose.yaml
.github/workflows/ci.yml
include/mybc/
  log.h
  net.h               # abstracció Winsock/POSIX
  ble/scanner.h
  ble/ride_decoder.h  # descodificador Zwift Ride
  ble/ride_bridge.h   # scan/connect/handshake/reconnect
  obc/protocol.h      # IDs, missatges i parser
  obc/server.h        # servidor TCP OBC
  obc/mdns.h          # anunci mDNS (DNS-SD)
src/
  main.cpp            # CLI: scan / inspect / serve
  log.cpp
  net.cpp
  ble/scanner.cpp
  ble/ride_decoder.cpp
  ble/ride_bridge.cpp
  obc/protocol.cpp
  obc/server.cpp
  obc/mdns.cpp
tests/test_protocol.cpp
tests/test_ride_decoder.cpp
docs/PLAN.md
```

---

## Llicències

- **OpenBikeControl protocol:** MIT.
- **SimpleBLE 1.3.0:** Business Source License 1.1, ús no comercial permès.
- **BikeControl:** *Non-Commercial*; es fa servir només com a referència del protocol
  del controlador, sense copiar codi.

Aquest projecte és personal i local: no envia dades enlloc.
