# Changelog

## 0.1.2 - 2026-11-14
- Het wekkergeluid voor de mediaspeler komt uit de eigen mediamap van Home Assistant, niet van internet: upload
  `media/tessera-alarm-clock.mp3` één keer onder Mijn media.

## 0.1.1 - 2026-11-14
- Een gekozen mediaspeler speelt nu de eigen piepjes van de wekker als **Af te spelen media** leeg is; eerst speelde
  hij dan niets. Het log van het scherm zegt wat het verstuurde.

## 0.1.0 - 2026-11-14
- Eerste versie: twee wekkers die je op het scherm instelt, elk met een tijd, zijn dagen en een aan/uit-toets.
- Een popup met Sluimeren en Stop als een wekker afgaat; een geluid op een scherm met een speaker, en op een
  mediaspeler van Home Assistant als je er een kiest.
- Elke wekker is een entiteit van het scherm in Home Assistant, met een sensor voor de volgende wekker en een die aan
  is terwijl hij afgaat.
