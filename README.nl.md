# Wekker

> Nieuw en nog niet op een echt scherm geprobeerd. Het scherm gaat aan en de wekker gaat af, ook 's nachts.

Twee wekkers op een Tessera-scherm, die je op het scherm zelf instelt: geen telefoon en geen Home Assistant nodig om
een wektijd te veranderen. Elke wekker heeft een tijd, de dagen waarop hij afgaat en een aan/uit-toets. Gaat er een af,
dan gaat het scherm aan met een popup met **Sluimeren** en **Stop**. Een scherm met een speaker laat ook geluid horen,
en een speaker van Home Assistant kan een geluid afspelen.

Elke wekker is ook een entiteit van het scherm in Home Assistant, zodat een automatisering een wekker kan lezen,
instellen en iets kan doen als hij afgaat.

## Instellen

1. Open in Tessera Plugins, kies **Add with a link** en plak
   `https://github.com/woldhek/tessera-plugin-alarm-clock`.
2. Vink het scherm aan en pas toe. **Mediaspeler** is optioneel: kies een speaker van Home Assistant in dezelfde
   kamer, of laat het veld leeg. Het scherm bouwt één keer.
3. Zet in Layout de tegel **Wekker** uit de groep Plugins op een pagina, in elke maat van 1x1 tot 2x2.
4. Tik op het scherm op de tegel. Kies **Wekker 1** of **Wekker 2**, stel de tijd in met **-** en **+**, tik de dagen
   aan en tik op de onderste toets om hem **Aan** te zetten.

## Op het scherm

- **De tegel** laat zien wanneer de volgende wekker afgaat: de dag en de tijd. Een tik opent de kaart.
- **De kaart** heeft vier rijen: welke wekker, de tijd (uren en minuten elk met een - en een +; de minuten gaan per
  vijf), de dagen van maandag tot zondag, en Aan of Uit. Elke tik wordt meteen bewaard: er is niets om op te slaan.
- **Geen dagen gekozen**: de wekker gaat één keer af, op zijn eerstvolgende tijd, en zet zichzelf uit.
- **Als een wekker afgaat** gaat het scherm aan, ook vanuit stand-by en 's nachts, met de popup. **Stop** beëindigt
  hem. **Sluimeren** laat hem een paar minuten later opnieuw afgaan; de sluittoets van de popup en een tik naast de
  popup sluimeren ook. Terwijl hij sluimert zegt de tegel dat, en stopt de onderste toets van de kaart hem.
- **Niemand in de buurt**: een wekker stopt vanzelf na tien minuten (een instelling), en een wekker die steeds weer
  sluimert geeft het op een uur nadat hij voor het eerst afging.

## Instellingen

Op het scherm: houd de bovenbalk vast, Settings, Plugins, Wekker. In Tessera: het tabblad Plugins van het scherm, de
details van de plugin, **Settings**. Dezelfde waarden zijn entiteiten van het scherm in Home Assistant.

- **Sluimeren**: toon de sluimertoets als een wekker afgaat (standaard aan). Uit: de popup heeft alleen Stop en komt
  terug tot iemand op Stop tikt.
- **Sluimertijd**: 1 tot 30 minuten, standaard 5.
- **Stopt vanzelf na**: 1 tot 60 minuten, standaard 10.
- **Wekkergeluid**: afgaan op de eigen speaker van het scherm. Alleen op een scherm met een speaker (hieronder).
- **Af te spelen media** en **Mediavolume**: wat de mediaspeler speelt en hoe hard (hieronder).
- **Wekker testen**: laat het scherm nu afgaan.

## Geluid

- **De eigen speaker van het scherm.** Op een scherm met een speaker, van het bord zelf (de Guition P4-panelen
  bijvoorbeeld) of van een andere plugin, staat het onderdeel **Afgaan op de speaker van het scherm** standaard aan:
  vier korte piepjes per seconde, op het volume van het scherm, tot de wekker stopt. Een scherm zonder speaker toont
  de popup zonder geluid. Op de Guition 4-inch maakt de plugin
  [Guition-speaker](https://github.com/woldhek/tessera-plugin-guition-speaker) van zijn speakeruitgang de speaker van
  het scherm. Een ander geluid: vervang `components/alarm_clock/sounds/alarm.wav` in een kopie van deze
  plugin door een WAV-bestand met 16-bit samples.
- **Een mediaspeler van Home Assistant.** Kies hem als je de plugin toevoegt (**Mediaspeler**). Meer hoeft niet: gaat
  een wekker af, dan speelt de speaker de eigen piepjes van de wekker, het bestand `media/alarm.mp3` van deze
  repository (tien minuten, opnieuw gestart als een wekker langer afgaat). Het komt van internet (jsDelivr), dus de
  speaker moet het kunnen bereiken. Iets anders afspelen: vul **Af te spelen media** in, met een adres zoals
  `https://example.org/wake.mp3`, een radiostream, of een media-source-id van Home Assistant zoals
  `media-source://media_source/local/wake.mp3`. Het scherm roept `media_player.play_media` aan op die speaker, na
  `media_player.volume_set` als **Mediavolume** boven 0 staat, en `media_player.media_stop` bij Stop en Sluimeren. Een
  eigen bestand speelt één keer: neem er een dat lang genoeg duurt. Hiervoor moet Home Assistant bereikbaar zijn op
  het moment dat de wekker afgaat.
- **Niets op de mediaspeler?** Druk in Home Assistant op **Ring alarm** en lees het log van het scherm (ESPHome, Logs):
  `Asked media_player... to play ...` betekent dat het scherm het verstuurd heeft en dat het probleem tussen Home
  Assistant en de speaker zit (het log van Home Assistant zegt waarom). `Not sent` betekent dat het scherm Home
  Assistant niet kon bereiken. Het scherm moet ook Home Assistant-acties mogen uitvoeren (Instellingen, Apparaten en
  diensten, ESPHome, het scherm, Configureren), zoals elke bediening van Tessera nodig heeft.

## In Home Assistant

Het apparaat van het scherm krijgt deze entiteiten (`<scherm>` is de naam van het scherm):

| Entiteit | Wat |
|---|---|
| `time.<scherm>_alarm_1_time`, `time.<scherm>_alarm_2_time` | De tijd van de wekker. Stel hem in met de actie `time.set_value`. |
| `switch.<scherm>_alarm_1`, `switch.<scherm>_alarm_2` | De wekker aan of uit. |
| `text.<scherm>_alarm_1_days`, `text.<scherm>_alarm_2_days` | De dagen: een of meer van `mon,tue,wed,thu,fri,sat,sun` met komma's ertussen, of `daily`, `weekdays`, `weekend`. Leeg: hij gaat één keer af. |
| `binary_sensor.<scherm>_alarm_ringing` | Aan terwijl een wekker afgaat, uit terwijl hij sluimert. |
| `sensor.<scherm>_next_alarm` | Wanneer de volgende wekker afgaat, als tijdstip. |
| `button.<scherm>_stop_alarm`, `button.<scherm>_snooze_alarm` | Stop of sluimer de wekker die afgaat. |
| `button.<scherm>_ring_alarm` | Laat het scherm nu afgaan. |

Een andere integratie zet de wektijd:

```yaml
action:
  - action: time.set_value
    target: { entity_id: time.slaapkamer_alarm_1_time }
    data: { time: "06:45:00" }
  - action: switch.turn_on
    target: { entity_id: switch.slaapkamer_alarm_1 }
```

Een notificatie, lampen of iets anders als de wekker afgaat:

```yaml
trigger:
  - trigger: state
    entity_id: binary_sensor.slaapkamer_alarm_ringing
    to: "on"
action:
  - action: notify.mobile_app_mijn_telefoon
    data: { message: "De wekker in de slaapkamer gaat af." }
```

## Goed om te weten

- De wekkers staan op het scherm en gaan af zonder Home Assistant, zolang het scherm zijn klok heeft. Home Assistant
  zet die klok na een start: een scherm dat start terwijl Home Assistant weg is, laat niets afgaan tot het terug is.
- Een wekker gaat af in de minuut van zijn tijd. Een scherm dat precies in die minuut herstart, mist hem.
- Elk scherm heeft zijn eigen twee wekkers.
- Een plugin kan de kaart niet op slot zetten: iedereen bij het scherm kan een wekker veranderen.
- Gecontroleerd met `check.py` van de plugins-repository en een compile tegen de headers van Tessera's release en dev,
  en getekend op een kleine testopstelling, niet op Tessera's preview-bench. Nog niet op een echt scherm gedraaid.
