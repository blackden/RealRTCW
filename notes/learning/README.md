# Learning materials

Глубокие учебные разборы render-кода RTCW. Не decision docs (не про
архитектурный выбор), не reference карта (не file:line указатели).

**Когда сюда заглядывать:**
- Забыл как работает конкретный эффект — освежить.
- Пришла пора портировать эффект в renderervk — открыть как «инструкцию по
  переводу».
- Учусь читать render-код в принципе — взять как образец того как разбирать.

**Стиль разборов:**
- Код построчно с пояснениями.
- C-идиомы (указатели, vec3_t, qboolean, битовые операции) разжёвываются.
- Сначала «что это в игровом смысле», потом «как это работает в коде»,
  потом «что значит для портирования».

**Содержимое:**

- [`fog-system-walkthrough.md`](fog-system-walkthrough.md) — обе fog-системы
  RTCW (distance/brush + global), `RB_CalcFogTexCoords`, `R_SetFog`,
  `R_Fog`. Создано 2026-06-09.

**Что планируется (по мере портирования эффектов):**

- corona walkthrough (M5.1 prep)
- smart skin / LOD walkthrough (M5.3 prep)
- zombieFX walkthrough (M5.4 prep)
- shader system overview (как .shader файлы становятся pipeline'ами)
