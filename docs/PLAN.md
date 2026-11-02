# Pla de projecte: MyBikeControl

> Pont local i privat en C++ entre el manillar **Zwift Ride / Play / Click** i **MyWhoosh**,
> parlant el protocol obert **OpenBikeControl**. El rodet **Wahoo KICKR Core** no es toca:
> el controla MyWhoosh amb **MyShift**.

- **Versió:** 0.2.0 (Fase 1 + descodificador Ride + integració al `serve`)
- **Data:** 2026-11-02
- **Plataforma objectiu:** Windows 10 1709+ / Windows 11, x64
- **Estat:** `scan`, `inspect` i `serve` implementats; `serve` connecta al Zwift Ride
  per BLE, descodifica els botons i els publica per mDNS/TCP. Sockets Winsock i
  DNS-SD natiu de Windows; mode `--demo` per provar sense manillar.

---

## 1. Objectiu i abast

Construir una eina d'escriptori nativa que:

1. Es connecti per BLE al manillar i descodifiqui les pulsacions de botons.
2. Les tradueixi a *button IDs* estàndard d'OpenBikeControl.
3. Es publiqui com a dispositiu OpenBikeControl (mDNS + TCP) davant de MyWhoosh.
4. MyWhoosh apliqui el canvi de marxa amb MyShift i mani el KICKR com sempre.

**Fora d'abast (MVP):** control directe del KICKR, interfície gràfica, núvol, comptes,
telemetria, suport d'altres rodets.

---

## 2. Correccions respecte del pla preliminar

| Tema | Pla preliminar | Realitat (nov. 2026) | Decisió |
| --- | --- | --- | --- |
| Capa de control | Enviar resistència al KICKR via FTMS | MyWhoosh 4.0+ té **MyShift** i ja controla el rodet; a més implementa OpenBikeControl natiu | **Pass-through**: només enviem `Shift Up` / `Shift Down` |
| Conflicte BLE | "Monopoli" resolt enviant ordres directes | Dues centrals escrivint al mateix *Fitness Machine Control Point* = *last-writer-wins* | **No toquem el KICKR**; zero conflicte |
| SimpleBLE | SimpleBLE per a tot el BLE | SimpleBLE **1.3.0** afegeix *local peripheral hosting*; el mode central ja existia | **mDNS/TCP** per MVP; BLE perifèric queda com a pla B |
| Fase 1 | R&D d'UUIDs i protocol | Spec pública + implementació de referència en Dart | **Portar** el desxifrat, no reinventar-lo |
| Nom | "OpenBikeControl" com a app | OpenBikeControl és el **protocol** i l'organització | L'app es diu **MyBikeControl** |
| Latència | Objectiu < 100-150 ms | BLE 7-15 ms + TCP local 20-50 ms segons la spec | Objectiu realista **< 60 ms**; mesurar |

### Per què no s'ha de controlar el KICKR

MyWhoosh ja envia ordres de resistència al KICKR segons el pendent, el mode ERG i la marxa
virtual. Si el bridge també hi escriu, les dues ordres es trepitgen i la resistència fa salts.
La solució correcta és **no ser un actor més**: ser només una font d'esdeveniments de canvi
de marxa i deixar que MyWhoosh calculi la resistència.

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
                                                        │ (o BLE perifèric, pla B)
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

## 4. Stack i decisions tècniques

| Element | Elecció | Motiu |
| --- | --- | --- |
| Llenguatge | C++17 | Requerit; suficient per a threads, sockets i BLE |
| Build | CMake ≥ 3.21 + MSVC 2022 | SimpleBLE 1.3 ho exigeix; MSVC per al backend WinRT |
| BLE central | SimpleBLE 1.3.0 | Multiplataforma, API simple, backend WinRT a Windows |
| Transport app | mDNS + TCP (Winsock) | C++ pur, sense APIs de perifèric, suportat per MyWhoosh |
| mDNS | DNS-SD natiu de Windows (`DnsServiceRegister`) | Zero dependències externes; Windows 10 1709+ |
| Concurrència | `std::thread`, `std::mutex`, `std::atomic` | Un fil per client TCP + un fil d'acceptació |
| Logs | logger propi amb timestamps | Sense dependències; `spdlog` és opcional més endavant |
| Config | JSON (Fase 3) | Mapatge de botons i ajustos sense recompilar |
| UI | CLI | El MVP no necessita interfície |

### Nota sobre SimpleBLE i llicències

SimpleBLE 1.3.0 es distribueix sota **Business Source License 1.1** amb *Additional Use Grant*
per a ús **no comercial** (projectes personals i hobby). Per a aquest projecte és adequat.
Si algun dia es vol comercialitzar, caldrà llicència alternativa o canviar de biblioteca.

---

## 5. Contracte OpenBikeControl implementat

### 5.1 Descoberta mDNS

- Tipus de servei: `_openbikecontrol._tcp.local.`
- Nom: `<Device Name>._openbikecontrol._tcp.local.`
- TXT: `version=1`, `id=<únic>`, `name=<nom>`, `service-uuids=d273f680-d548-419d-b9d1-fa0472345229`,
  `manufacturer=<nom>`, `model=<model>`
- TCP: `<ip>:<port>` (per defecte 8080)

A Windows s'implementa amb `DnsServiceRegister` / `DnsServiceConstructInstance`
(`dnsapi.lib`). El nom d'instància és el FQDN anterior i el host és
`<nom-equip>.local`. A macOS es manté `dns_sd` com a plataforma secundària.

### 5.2 Missatges

| Tipus | Direcció | Format |
| --- | --- | --- |
| `0x01` Button State | bridge → app | `[0x01, id, state, id, state, ...]` |
| `0x02` Device Status | bridge → app | `[0x02, battery, connected]` (cada 30 s) |
| `0x03` Haptic Feedback | app → bridge | `[0x03, pattern, duration, intensity]` |
| `0x04` App Info | app → bridge | `[0x04, ver, idLen, id..., verLen, ver..., count, ids...]` |

**Framing TCP:** la spec no defineix prefix de longitud per als missatges `0x01`.
Convenció adoptada (la mateixa que la implementació de referència): **un `send()` per missatge**
amb `TCP_NODELAY`. El parser d'entrada és incremental i tolerant, perquè `0x03` i `0x04` sí que
es poden reconstruir.

### 5.3 Button IDs que farà servir el pont

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

Els botons no suportats per l'app s'ignoren gràcies al missatge `0x04` (App Info), que arriba
just després de connectar.

---

## 6. Fases

### Fase 0 — Validació prèvia (30 min, opcional)

**Objectiu:** confirmar que MyWhoosh descobreix un dispositiu OBC abans d'escriure més codi.

- Executar l'exemple Python `examples/python/mock_device_tcp.py` del repo del protocol.
- Obrir MyWhoosh → Connection → icona OpenBikeControl → confirmar.
- Comprovar que apareix i que els botons simulats fan canviar de marxa.

**Criteri d'acceptació:** MyWhoosh mostra el dispositiu i respon als `Shift Up/Down`.

### Fase 1 — Descoberta BLE (feta en aquesta versió)

**Objectiu:** identificar el controlador i els seus serveis/característiques.

- `mybc scan` llista dispositius amb nom, adreça, RSSI i serveis anunciats.
- `mybc inspect <index>` connecta i bolca l'arbre GATT.
- `mybc inspect <index> --notify` subscriu a totes les característiques notificables i
  hexdumpeja els payloads amb timestamp.

**Criteri d'acceptació:** prémer els botons del Ride produeix notificacions identificables
amb el botó premut i alliberat.

### Fase 2 — Descodificació del controlador (feta per al Zwift Ride)

**Objectiu:** convertir els payloads BLE en esdeveniments de botó.

> Implementat a `src/ble/ride_decoder.cpp` amb tests de payloads reals. Zwift
> Play/Click queden pendents.

Referència principal (Dart, només per a ús personal): `lib/bluetooth/devices/zwift/`
del repo de BikeControl, amb variants `zwift_ride.dart`, `zwift_play.dart`,
`zwift_play_fw2.dart`, `zwift_clickv2.dart`, `zwift_clickv2_left_side.dart`,
`zwift_clickv2_right_side.dart`, `zwift_unlock.dart`.

Tasques:

1. Implementar un descodificador per a la variant detectada (nom/servei/firmware).
2. Debounce per contacte (no per pulsació: cada pressió ha de generar press + release).
3. Log de cada transició amb nom de botó i timestamp.
4. Tests unitaris amb payloads reals capturats amb `--notify`.

**Criteri d'acceptació:** cada botó físic genera exactament una parella press/release i
cap event espuri en repòs.

### Fase 3 — Pont i mapatge (integrat; configuració pendent)

**Objectiu:** connectar el descodificador al servidor OBC.

> `RideBridge` connecta, fa el handshake i fusiona les dues meitats. El mapatge
> és fix per al Ride; el `config.json` encara no existeix.

- `config.json` amb mapatge `payload → button ID` i opcions de debounce.
- Enviar `0x01` només en canvi d'estat, amb tots els canvis simultanis en un sol missatge.
- Filtrar per App Info: si l'app no suporta `0x01`/`0x02`, no enviar res o fer fallback.
- Mode `--demo` (ja implementat) per validar sense hardware.

**Criteri d'acceptació:** prémer `+` al Ride canvia la marxa a MyWhoosh en menys de 60 ms.

### Fase 4 — Integració i latència

- Mesurar de puny a canvi de resistència: log de timestamp al bridge + observació a MyWhoosh.
- Optimitzar: un `send()` per missatge, `TCP_NODELAY`, cap operació bloquejant al camí crític.
- Provar amb MyWhoosh en el mateix PC Windows i en un altre dispositiu de la xarxa.

**Criteri d'acceptació:** latència percebuda < 100 ms; mesurada < 60 ms en local.

### Fase 5 — Robustesa

- Reconnectar el controlador automàticament si es perd (backoff exponencial).
- Suportar diverses connexions TCP simultànies i netejar clients morts.
- Enviar Device Status cada 30 s i en connectar.
- Gestionar `SIGINT`/`SIGTERM` amb tancament net.
- Rotació/verbositat de logs (`--verbose`).

**Criteri d'acceptació:** desconnectar i reconnectar el manillar no requereix reiniciar el bridge.

### Fase 6 — Empaquetat Windows

- Distribuir un sol `mybc.exe` (o un instal·lador) amb les dependències correctes:
  - opció A: runtime dinàmic de MSVC + VC++ Redistributable;
  - opció B: runtime estàtic (`/MT`) per evitar dependències.
- Firewall: documentar i, si es pot, afegir la regla d'entrada per TCP 8080 i UDP 5353
  en instal·lar.
- Opcional: icona a la safata, arrencada automàtica i mode servei de Windows.
- Signatura de codi opcional per evitar avisos de SmartScreen.

**Criteri d'acceptació:** doble clic a l'executable, permís de firewall concedit una
vegada, i funciona sense Terminal.

---

## 7. Riscos i mitigacions

| Risc | Impacte | Mitigació |
| --- | --- | --- |
| El controlador només accepta connexions de Zwift o requereix *bonding* | Alt | Fase 1 amb `scan`/`inspect`; si cal, estudiar `zwift_unlock.dart` i el firmware suportat |
| MyWhoosh no descobreix el servei mDNS | Mitjà | Fase 0 amb el mock Python; comprovar firewall, perfil de xarxa **Private** i UDP 5353; si falla, provar BLE perifèric (SimpleBLE 1.3 `create_local_peripheral`) |
| Firewall de Windows o xarxa **Public** bloqueja mDNS/TCP | Mitjà | Afegir `mybc.exe` a les excepcions, xarxa privada, ports TCP 8080 i UDP 5353; documentar-ho |
| `DnsServiceRegister` no existeix (Windows < 1709) | Baix | Requerir Windows 10 1709+ o Windows 11; detectar i mostrar un error clar |
| El backend WinRT de SimpleBLE falla o no veu l'adaptador | Mitjà | CI a `windows-latest`; comprovar Bluetooth i permisos; `scan` com a diagnòstic |
| El protocol del controlador canvia segons firmware | Mitjà | Implementacions de referència per variant + tests amb payloads reals |
| Framing TCP ambigu (sense length prefix) | Mitjà | Un `send()` per missatge, `TCP_NODELAY`, parser tolerant, no coalescer |
| Latència | Baix | Fil dedicat, cues lock-free al camí crític, mesurarem |
| Llicències | Baix | OBC MIT; SimpleBLE BSL no comercial; BikeControl no comercial |
| MyWhoosh canvia el protocol | Baix | És un protocol obert i versionat; el TXT porta `version=1` |

---

## 8. Llicències i ètica

- **OpenBikeControl protocol:** MIT. Es pot implementar lliurement.
- **SimpleBLE 1.3.0:** Business Source License 1.1, ús no comercial permès. Adequat per a
  aquest projecte personal; per a producte cal llicència alternativa.
- **BikeControl:** les versions actuals són *Non-Commercial*; les anteriors al tag `gpl3`
  eren GPL-3.0. Es farà servir **només com a referència** per entendre el protocol del
  controlador, sense copiar codi en una distribució.
- El projecte és local i privat: no envia dades enlloc, no fa telemetria i no interacciona
  amb els servidors de Zwift.

---

## 9. Referències

- Protocol: <https://github.com/OpenBikeControl/openbikecontrol-protocol>
  - `PROTOCOL.md`, `BLE.md`, `MDNS.md`
  - Exemples Python: `examples/python/mock_device_tcp.py`, `mdns_trainer_app.py`
- BikeControl (referència del costat controlador): <https://github.com/OpenBikeControl/bikecontrol>
- SimpleBLE: <https://github.com/OpenBluetoothToolbox/SimpleBLE>
- Windows DNS-SD: <https://learn.microsoft.com/windows/win32/api/windns/nf-windns-dnsserviceregister>
  i <https://learn.microsoft.com/windows/win32/api/windns/nf-windns-dnsserviceconstructinstance>
- Winsock: <https://learn.microsoft.com/windows/win32/winsock/windows-sockets-start-page-2>
- MyShift (shifting virtual de MyWhoosh): <https://mywhoosh.com/myshift-mywhooshs-virtual-shifting-explained/>
- Compatibilitat BikeControl + MyWhoosh: <https://bikecontrol.app/use-zwift-play-with-mywhoosh/>

---

## 10. Decisions pendents

1. **Variant de firmware** del Zwift Ride/Play a suportar primer (cal capturar payloads).
2. **Nombre de marxes** i desmultiplicacions: les gestiona MyShift; el bridge no les toca.
3. **Haptic feedback:** el manillar pot vibrar? Si sí, mapejar els patrons `0x03` a la
   característica corresponent del controlador.
4. **BLE perifèric** a més de mDNS: útil si MyWhoosh falla per xarxa o per a apps que només
   suporten OBC per BLE (Biketerra, Ride Cave).
5. **Distribució Windows:** `.exe` amb runtime dinàmic + VC++ Redistributable, o runtime
   estàtic; instal·lador/MSIX, signatura i arrencada automàtica.
6. **Mode d'execució:** aplicació de consola, safata del sistema o servei de Windows.
