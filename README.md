# MyBikeControl

Pont local i privat entre el manillar **Zwift Ride / Play / Click** i **MyWhoosh**, en C++.

El bridge llegirà els botons del manillar per BLE i els publicarà com a dispositiu
**OpenBikeControl** (mDNS + TCP). MyWhoosh els rebrà i aplicarà el canvi de marxa amb
**MyShift**. El **Wahoo KICKR Core** no es toca: el continua controlant MyWhoosh.

```
Zwift Ride/Play --BLE--> MyBikeControl --OpenBikeControl--> MyWhoosh --FTMS--> KICKR
```

> **Estat:** projecte en construcció. Aquest PR introdueix el pla i l'abast; el codi
> arriba als PRs següents.

Pla complet, decisions i fases: [`docs/PLAN.md`](docs/PLAN.md).

## Desenvolupament

Requisits: CMake ≥ 3.21 i un compilador C++17.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Aquest primer pas afegeix la llibreria `mybc_core` amb el codec i el parser
d'OpenBikeControl, més els tests unitaris. Encara no hi ha executable.
