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

Pla complet i decisions: [`docs/PLAN.md`](docs/PLAN.md).

---

## Estat

| Fase | Estat |
| --- | --- |
| 1. Descoberta BLE (`scan`, `inspect --notify`) | ✅ implementat |
| Transport OpenBikeControl (`serve`, mDNS/TCP, mode `--demo`) | ✅ implementat |
| 2. Descodificació del controlador Zwift | ⏳ següent |
| 3. Pont i mapatge configurable | ⏳ |
| 4. Integració i latència | ⏳ |
| 5. Robustesa | ⏳ |
| 6. Empaquetat macOS | ⏳ |

---

## Requisits

- macOS 13+ (Apple Silicon o Intel) amb Bluetooth.
- Xcode Command Line Tools (`xcode-select --install`).
- CMake ≥ 3.21.
- Connexió a internet la primera vegada (CMake baixa SimpleBLE 1.3.0).

---

## Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Si no vols BLE (per exemple, per compilar només el transport):

```bash
cmake -S . -B build -DMYBC_WITH_BLE=OFF
```

---

## Ús

### 1. Descobrir el manillar

```bash
./build/mybc scan --seconds 10
```

El manillar ha d'estar despert i **no connectat a Zwift**. Prem un botó per despertar-lo.

### 2. Inspeccionar serveis i capturar botons

```bash
./build/mybc inspect 0 --notify --seconds 30
```

Mostra l'arbre GATT i hexdumpeja cada notificació. Prem els botons durant la captura:
aquestes dades són les que faran servir per implementar el descodificador de la Fase 2.

### 3. Provar el pont sense manillar

```bash
./build/mybc serve --demo
```

Publica el dispositiu per mDNS i alterna `Shift Up`/`Shift Down` cada 3 segons.
És la manera més ràpida de validar la integració amb MyWhoosh.

### 4. Executar el pont

```bash
./build/mybc serve --name "MyBikeControl"
```

Opcions:

| Opció | Descripció |
| --- | --- |
| `--port N` | Port TCP (per defecte 8080; `0` = aleatori) |
| `--name S` | Nom visible a MyWhoosh |
| `--id S` | Identificador únic del TXT mDNS |
| `--no-mdns` | No anunciar per mDNS (proves manuals) |
| `--demo` | Generar canvis de marxa sintètics |
| `--verbose` | Logs de debug |

---

## Configurar MyWhoosh

1. Obre **Connection**.
2. Toca la icona **OpenBikeControl** a dalt a la dreta.
3. Confirma el popup.
4. Comença a pedalar: els botons de canvi han de moure la marxa de MyShift.

El KICKR segueix emparellat a MyWhoosh com sempre. No el connectis al bridge.

---

## Permisos de macOS

La primera vegada, macOS demanarà permisos a l'app que executa el binari (Terminal o iTerm):

- **Bluetooth** — per connectar-se al manillar.
- **Xarxa local** — per anunciar-se per mDNS i acceptar connexions TCP.

Si els denegues, ves a *System Settings → Privacy & Security* i torna'ls a activar.
En una versió empaquetada, aquests permisos aniran declarats a l'`Info.plist`
(`NSBluetoothAlwaysUsageDescription`, `NSLocalNetworkUsageDescription`,
`NSBonjourServices`).

---

## Resolució de problemes

| Símptoma | Causa probable | Solució |
| --- | --- | --- |
| `No Bluetooth adapter found` | Bluetooth apagat o permís denegat | Activa'l i revisa *Privacy & Security* |
| El manillar no apareix a `scan` | Està connectat a Zwift o adormit | Desconnecta'l de Zwift i prem un botó |
| MyWhoosh no veu el bridge | mDNS bloquejat o xarxa diferent | Mateix Wi-Fi, revisa el permís de xarxa local i el firewall |
| La marxa no canvia | MyShift desactivat o app no connectada | Comprova el log `app connected` i l'estat de MyShift |
| La resistència fa salts | Algú està controlant el KICKR directament | Desconnecta el bridge del KICKR; només MyWhoosh hi ha de parlar |

---

## Docker

La imatge final executa l'app. Per defecte arrenca el servidor en mode demo, que no
necessita Bluetooth:

```bash
docker build -t mybc .
docker run --rm -p 8080:8080 mybc
# equivalent:
docker compose up serve-demo
```

El servidor escolta a `0.0.0.0:8080` i envia `Shift Up`/`Shift Down` cada 3 segons.
Com que va amb `--no-mdns`, MyWhoosh no el descobrirà sol: serveix per provar el
transport amb qualsevol client TCP.

### Què funciona a cada entorn

| Entorn | BLE | mDNS | Ús |
| --- | --- | --- | --- |
| macOS natiu | ✅ | ✅ (`dns_sd`) | Ús real amb MyWhoosh |
| Docker Desktop (macOS/Windows) | ❌ | ❌ | Demo TCP i tests |
| Linux amb `network_mode: host` | ⚠️ Fase 2 | ❌ (encara stub) | Servidor OBC; falten BLE i Avahi |

Docker Desktop corre els contenidors en una VM Linux sense el Bluetooth de
l'amfitrió, i el mDNS no travessa la VM per defecte. Per això el runtime real amb
MyWhoosh és natiu al Mac.

En un Linux amb Bluetooth es pot provar el servei `serve` (xarxa de l'amfitrió i
dispositius muntats):

```bash
docker compose up serve
```

### CI i tests

```bash
docker compose run --rm test          # ctest amb MYBC_WITH_BLE=OFF
docker compose build ble-compile      # compila amb SimpleBLE (MYBC_WITH_BLE=ON)
docker compose run --rm version       # mybc version
```

## Estructura

```
CMakeLists.txt
Dockerfile
compose.yaml
include/mybc/
  log.h
  ble/scanner.h
  obc/protocol.h      # IDs, missatges i parser
  obc/server.h        # servidor TCP OBC
  obc/mdns.h          # anunci mDNS (dns_sd)
src/
  main.cpp            # CLI: scan / inspect / serve
  log.cpp
  ble/scanner.cpp
  obc/protocol.cpp
  obc/server.cpp
  obc/mdns.cpp
tests/test_protocol.cpp
docs/PLAN.md
```

---

## Llicències

- **OpenBikeControl protocol:** MIT.
- **SimpleBLE 1.3.0:** Business Source License 1.1, ús no comercial permès.
- **BikeControl:** *Non-Commercial*; es fa servir només com a referència del protocol
  del controlador, sense copiar codi.

Aquest projecte és personal i local: no envia dades enlloc.
