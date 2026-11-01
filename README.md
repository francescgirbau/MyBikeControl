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
