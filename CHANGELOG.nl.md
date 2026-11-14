# Changelog

## 0.2.0 - 2026-11-14
- Een nieuw uiterlijk: de tijd met pijltjes omhoog en omlaag, ronde dagtoetsen, een schakelaar die zegt wanneer de
  wekker afgaat, en beide wekkers op een tegel van 2x1 of groter.
- Elke wekker heeft een eigen sensor die aan is terwijl hij afgaat, voor automatiseringen; de popup noemt de wekker.
- De mediaspeler is uit de plugin: speel elk geluid op elke speaker vanuit een automatisering op de sensor (zie de
  README). Haal het bestand onder Mijn media weg als je dat had neergezet.

## 0.1.3 - 2026-11-14
- Het wekkergeluid voor de mediaspeler is één piepcyclus (een paar kB, geen tien minuten), elke drie seconden
  opnieuw gespeeld zolang de wekker afgaat, op elke mediaspeler. Upload de nieuwe `media/tessera-alarm-clock.mp3`
  onder Mijn media.

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
