# Fog в RTCW — глубокий разбор

**Создано:** 2026-06-09
**Для:** будущего меня (ragnar), как учебный материал для возврата
**Стиль:** код построчно с пояснениями каждой строки/идиомы
**Что покрывает:** обе fog-системы RTCW (distance/brush + global), full code walkthrough, что это значит для Vulkan порта

> Этот файл — не decision, не reference карта. Это **обучающий разбор**.
> Открывать когда: (а) забыл как fog работает и надо освежить, (б) пришло
> время портировать fog в renderervk (M5.2 по roadmap), (в) другой эффект
> учусь и хочется образца «как разбирать render код».

## Оглавление

- [Часть 1. Что такое fog в смысле игровой графики](#часть-1-что-такое-fog-в-смысле-игровой-графики)
- [Часть 2. Distance fog (brush-based)](#часть-2-distance-fog-brush-based)
  - [Структуры данных](#структуры-данных)
  - [Откуда fog volume берётся](#откуда-fog-volume-берётся)
  - [RB_CalcFogTexCoords — главная функция distance fog](#rb_calcfogtexcoords--главная-функция-distance-fog)
- [Часть 3. Global fog (RTCW addition)](#часть-3-global-fog-rtcw-addition)
  - [glfog_t struct](#glfog_t-struct)
  - [Множество fog slots](#множество-fog-slots)
  - [R_SetFog — game зовёт когда хочет поменять fog](#r_setfog--game-зовёт-когда-хочет-поменять-fog)
  - [R_Fog — каждый кадр перед рисованием](#r_fog--каждый-кадр-перед-рисованием)
  - [R_FogOn / R_FogOff](#r_fogon--r_fogoff)
- [Часть 4. Что значит «портировать fog» на Vulkan](#часть-4-что-значит-портировать-fog-на-vulkan)
- [Часть 5. Итого по объёму fog портирования](#часть-5-итого-по-объёму-fog-портирования)
- [Что в этом коде неочевидно но важно](#что-в-этом-коде-неочевидно-но-важно)

---

## Часть 1. Что такое fog в смысле игровой графики

В реальной жизни туман это маленькие капельки воды в воздухе. Свет от далёких предметов **рассеивается** на этих капельках по пути к глазу — поэтому далёкое серое и нечёткое, ближнее норм. Это физика.

В игре физику никто не моделирует. Вместо неё **подделка**: для каждого пикселя на экране смотрим «насколько он далеко от камеры» и смешиваем его реальный цвет с серым (или каким настроили) пропорционально расстоянию. Чем дальше — тем больше серого. Глаз обманывается, мозг говорит «о, туман».

Это базовая идея. Дальше начинается интересное.

В RTCW есть **два совершенно разных fog'а** живущих параллельно:

1. **Distance fog / brush fog** (наследие Quake3) — туман привязан к **объёмам в карте**. Художник уровня рисует кубики в редакторе и говорит «вот тут туман плотностью X, цветом Y, глубиной Z». Когда камера смотрит сквозь этот кубик, поверхности за ним становятся туманнее. Это локальный туман — может быть в одной комнате, и не быть в другой. Используется для подвалов катакомб, тёмных коридоров.

2. **Global fog / RTCW fog** (добавили в RTCW, в Quake3 нет) — туман на весь мир сразу, без привязки к объёмам. Может **меняться во времени**: ровно идёт по карте, в момент cutscene'а меняется на красный (взрыв), потом плавно возвращается. Используется для уличных уровней (снег, лес), погодных переходов, cinematic'ов.

Они работают по совершенно разной механике. Разбираем оба.

---

## Часть 2. Distance fog (brush-based)

### Структуры данных

`code/renderer/tr_local.h:380`:

```c
typedef struct {
    vec3_t color;
    float depthForOpaque;
} fogParms_t;
```

**Построчно:**
- `typedef struct { ... } fogParms_t;` — определяем новый тип данных `fogParms_t`. Это «коробка» в которой лежат поля.
- `vec3_t color;` — **что такое `vec3_t`?** Это `typedef float vec3_t[3]` — массив из трёх `float`'ов. По соглашению в Q3 это вектор/цвет: либо координаты в пространстве, либо RGB. Здесь — RGB цвет тумана от 0.0 до 1.0 (например {0.5, 0.5, 0.7} — серый с лёгкой синевой).
- `float depthForOpaque;` — на каком расстоянии (в игровых единицах) туман становится **полностью непрозрачным**. Меньше — гуще, больше — реже. Если 100 — то всё что дальше 100 единиц от глубины fog brush'а уже неразличимо.

Это **то что прописано в .shader файле художником**. Статично, не меняется в run-time.

Теперь `tr_local.h:549`:

```c
typedef struct {
    int originalBrushNumber;
    vec3_t bounds[2];

    unsigned colorInt;
    float tcScale;
    fogParms_t parms;

    qboolean hasSurface;
    float surface[4];
} fog_t;
```

Это уже **runtime data** про один конкретный fog volume в загруженной карте. Построчно:

- `int originalBrushNumber;` — `int` это целое число. Brush'и в BSP пронумерованы — этот id помогает дебажить «который brush мне делает странный туман».
- `vec3_t bounds[2];` — bounding box volume'а: `bounds[0]` это {min_x, min_y, min_z}, `bounds[1]` это {max_x, max_y, max_z}. Куб охватывающий volume.
- `unsigned colorInt;` — **что такое `unsigned`?** Сокращение от `unsigned int` — целое без знака, 32 бита. Здесь упакованный RGBA: байт красного, байт зелёного, байт синего, байт альфы — все в одном слове. Используется для быстрой передачи цвета в вершинах (потому что vec3 floats = 12 байт, а упакованный int = 4 байта). Это **тот же цвет что `parms.color`**, но в форме которую GPU быстро жрёт.
- `float tcScale;` — про это будет подробно в следующем секции. Коротко: scaling factor который преобразует мировое расстояние в координату текстуры. Чем меньше — тем «дальше тянется» туман.
- `fogParms_t parms;` — **вложенная структура** (которую мы только что разобрали). `parms.color` (цвет) и `parms.depthForOpaque` (на каком расстоянии полная непрозрачность).
- `qboolean hasSurface;` — **что такое `qboolean`?** В Q3 определено как `typedef enum { qfalse, qtrue } qboolean` — целое со значением 0 или 1. У C нет нативного булевого типа в эпоху Q3 (тип `bool` появился в C99, Q3 старше). Это поле: «есть ли у этого fog volume плоскость-граница». Не у всех есть — global fog не имеет, brush fog имеет.
- `float surface[4];` — **плоскость** описана 4 float'ами в формате `[Nx, Ny, Nz, d]` где `[Nx, Ny, Nz]` — нормаль (вектор перпендикулярный плоскости длины 1), а `d` — расстояние от начала координат до плоскости. Это математическое **уравнение плоскости** в виде `Nx*x + Ny*y + Nz*z + d = 0`. Если точка `(x,y,z)` удовлетворяет — она на плоскости. Если `> 0` — над плоскостью. Если `< 0` — под.

Зачем плоскость? Потому что fog volume в RTCW не «вся комната туманная», а **«ниже определённой высоты туман»**. Например в катакомбах: на полу густой туман, на потолке чистый воздух. Эта плоскость отделяет одно от другого.

### Откуда fog volume берётся

Карта (`.bsp` файл) при загрузке содержит fog brushes. Engine читает их в `code/renderer/tr_bsp.c` и заполняет массив `tr.world->fogs[]`. Каждая поверхность (`msurface_t`) в карте знает свой `fogIndex` — указатель «я внутри fog volume номер N».

**В рантайме** когда renderer собирается рисовать поверхность:
```c
tess.fogNum = surface->fogIndex;  // запомнили какой fog для этой поверхности
```

И позже в pipeline, когда уже вершины поверхности обрабатываются — вызывается `RB_CalcFogTexCoords`. Идём туда.

### RB_CalcFogTexCoords — главная функция distance fog

Это **сердце** brush fog'а. Функция считает для каждой вершины поверхности: «какие texture coordinates ей дать, чтобы потом fog texture сделала правильный fade».

**Внимание, хитрый трюк:** вместо того чтобы для каждого пикселя считать сколько fog'а его покрывает (per-pixel math = медленно), Q3 использует **1D градиентную текстуру** (просто полоска от прозрачного к непрозрачному) и хитро подбирает texture coordinates так, что когда GPU интерполирует эту текстуру между вершинами — оно естественно даёт правильный fog.

Это elegant'ный, типично-quake-эры хак. Никакой fog physics — чистая алгебра + GPU's texture interpolation.

Файл `code/renderer/tr_shade_calc.c:853`:

```c
void RB_CalcFogTexCoords( float *st ) {
    int i;
    float       *v;
    float s, t;
    float eyeT;
    qboolean eyeOutside;
    fog_t       *fog;
    vec3_t local;
    vec4_t fogDistanceVector, fogDepthVector = {0, 0, 0, 0};
```

**Построчно по сигнатуре и переменным:**

- `void RB_CalcFogTexCoords( float *st )` — функция принимает указатель `st` на массив `float`'ов. Это «выходной» буфер: функция запишет в него `s` (s-coordinate) и `t` (t-coordinate) для каждой вершины. **Что такое `s` и `t`?** Это стандартные имена для **texture coordinates** — два числа от 0 до 1 говорящих «какую точку текстуры брать». Точка `(0, 0)` — левый-нижний угол текстуры, `(1, 1)` — правый-верхний. Здесь массив хранит пары: `st[0]=s0, st[1]=t0, st[2]=s1, st[3]=t1, ...` для каждой вершины.
- `int i;` — счётчик цикла (стандарт).
- `float *v;` — указатель на текущую вершину которую мы обрабатываем. Будет ходить по vertex buffer'у.
- `float s, t;` — рассчитанные texture coords для текущей вершины (потом запишутся в `st`).
- `float eyeT;` — texture coordinate глаза (камеры). Используется для определения «глаз внутри fog volume или снаружи».
- `qboolean eyeOutside;` — true/false: камера снаружи fog volume'а или внутри.
- `fog_t *fog;` — указатель на структуру fog'а, который мы сейчас обрабатываем.
- `vec3_t local;` — временный 3D вектор.
- `vec4_t fogDistanceVector;` — **4D вектор**. Это математический трюк (homogeneous coords): `fogDistanceVector[0..2]` — направление в пространстве, `fogDistanceVector[3]` — offset. Когда вычисляешь `DotProduct(v, dir) + offset` это эквивалентно «как далеко вершина v вдоль направления dir с привязкой к началу».
- `vec4_t fogDepthVector = {0, 0, 0, 0};` — то же самое, но вдоль нормали fog plane (не вдоль view direction). Инициализирован нулями на случай если у fog'а нет плоскости.

Далее:

```c
    fog = tr.world->fogs + tess.fogNum;
```

`tr.world` — указатель на текущую загруженную карту. `->fogs` — массив fog volume'ов в карте. `tess.fogNum` — индекс fog'а в котором сейчас рисуемая поверхность. Знакомая C-идиома: `array + index` это указатель на N-ный элемент (эквивалентно `&array[index]`).

```c
    // all fogging distance is based on world Z units
    VectorSubtract( backEnd.or.origin, backEnd.viewParms.or.origin, local );
```

Это **препроцессорный** макрос: `VectorSubtract(a, b, out) → out[0]=a[0]-b[0], out[1]=a[1]-b[1], out[2]=a[2]-b[2]`. Вычисляет вектор-разность двух точек.

**Что в нём:**
- `backEnd.or.origin` — позиция текущего **объекта** который рисуется (например модели или brush'а). `backEnd` — глобальная структура «то что бэкенд рендерит сейчас». `.or` это **orientation** — позиция + вращение объекта. `.origin` — конкретно позиция.
- `backEnd.viewParms.or.origin` — позиция **камеры** в мире.
- `local` — выходной вектор. Результат: позиция объекта относительно камеры.

Зачем? Дальше будет матрица модели, и нам надо привязать вычисления к объекту, а не к мировой системе координат.

```c
    fogDistanceVector[0] = -backEnd.or.modelMatrix[2];
    fogDistanceVector[1] = -backEnd.or.modelMatrix[6];
    fogDistanceVector[2] = -backEnd.or.modelMatrix[10];
    fogDistanceVector[3] = DotProduct( local, backEnd.viewParms.or.axis[0] );
```

Это **выдёргивание view direction** из матрицы модели. `modelMatrix` это 4×4 матрица как массив 16 float'ов в column-major order: индексы `[0,1,2,3]` — первый столбец, `[4,5,6,7]` — второй, и т.д. Индексы `[2,6,10]` — это **третий ряд** матрицы, который представляет «куда смотрит камера в системе координат объекта». Знак минус потому что Q3 использует convention где Z-ось это «вперёд» с обратным знаком.

`DotProduct(a, b)` — **скалярное произведение** двух 3D векторов: `a[0]*b[0] + a[1]*b[1] + a[2]*b[2]`. Геометрически: если b это единичный вектор, то результат это «проекция a на направление b». Здесь: насколько `local` (позиция объекта от камеры) проецируется на ось `viewParms.or.axis[0]` (forward направление камеры). Получаем «как глубоко объект перед камерой».

Это `[3]` компонента — offset для homogeneous coord трюка. Когда дальше будет `DotProduct(vertex, fogDistanceVector) + fogDistanceVector[3]` — это даст «расстояние от камеры до вершины вдоль view direction».

```c
    fogDistanceVector[0] *= fog->tcScale;
    fogDistanceVector[1] *= fog->tcScale;
    fogDistanceVector[2] *= fog->tcScale;
    fogDistanceVector[3] *= fog->tcScale;
```

Масштабируем весь вектор на `tcScale`. **Зачем?** Это конвертация «мировые единицы расстояния» → «texture coordinate». Если `tcScale = 1/256`, то 256 мировых единиц = 1.0 texture coord (один полный «прогон» fog ramp). Чем меньше `tcScale`, тем дальше можно отойти прежде чем туман станет полным.

Этот scaling важен потому что fog texture в результате будет растягиваться/сжиматься: после умножения, `DotProduct(vertex, fogDistanceVector) + offset` сразу даст **готовую texture coordinate**, не «расстояние которое надо потом конвертировать».

Дальше — **обработка fog plane** (если есть):

```c
    if ( fog->hasSurface ) {
        fogDepthVector[0] = fog->surface[0] * backEnd.or.axis[0][0] +
                            fog->surface[1] * backEnd.or.axis[0][1] +
                            fog->surface[2] * backEnd.or.axis[0][2];
        fogDepthVector[1] = fog->surface[0] * backEnd.or.axis[1][0] +
                            fog->surface[1] * backEnd.or.axis[1][1] +
                            fog->surface[2] * backEnd.or.axis[1][2];
        fogDepthVector[2] = fog->surface[0] * backEnd.or.axis[2][0] +
                            fog->surface[1] * backEnd.or.axis[2][1] +
                            fog->surface[2] * backEnd.or.axis[2][2];
        fogDepthVector[3] = -fog->surface[3] + DotProduct( backEnd.or.origin, fog->surface );

        eyeT = DotProduct( backEnd.or.viewOrigin, fogDepthVector ) + fogDepthVector[3];
    } else {
        eyeT = 1;
    }
```

**Что тут происходит:** мы **поворачиваем** fog plane normal (`fog->surface[0..2]`) из мирового пространства в локальное пространство объекта. `backEnd.or.axis[]` это 3 базисных вектора локальной системы координат. Скалярное произведение нормали с каждым базисным вектором даёт компоненты нормали в локальных координатах. Это аналог matrix multiplication, расписанный вручную.

`fogDepthVector[3]` — offset плоскости с учётом translation объекта.

`eyeT` — **где камера относительно fog plane**. `DotProduct(viewOrigin, depthVector) + depthVector[3]` — стандартная формула «расстояние от точки до плоскости». Если положительное — камера над плоскостью (вне fog volume), если отрицательное — под (внутри fog volume).

Если у fog'а нет плоскости (`hasSurface = false`) — `eyeT = 1` означает «камера всегда внутри». Это global-style fog без явной границы.

```c
    if ( eyeT < 0 ) {
        eyeOutside = qtrue;
    } else {
        eyeOutside = qfalse;
    }
```

Записываем «снаружи ли камера» в булевое поле. Используем дальше.

```c
    fogDistanceVector[3] += 1.0 / 512;
```

Хитрая магическая константа. `1/512` это очень маленький offset — сдвигает начало fog ramp немного, чтобы избежать **z-fighting** на самой первой вершине (где иначе texture coord был бы ровно 0 и могло бы flicker'ить). Это типичный «epsilon hack» эпохи.

Теперь **главный цикл** — per-vertex:

```c
    for ( i = 0, v = tess.xyz[0] ; i < tess.numVertexes ; i++, v += 4 ) {
```

`for` с тремя инициализациями: `i = 0` и одновременно `v = tess.xyz[0]` — указатель на первую вершину. **Что такое `tess.xyz`?** Это массив всех вершин которые мы сейчас собираемся рисовать. `tess.xyz[N]` — координата N-ной вершины как `vec4_t` (xyz + padding для alignment). `i++` каждую итерацию + `v += 4` — продвигаем указатель на 4 float'а (что соответствует одной vec4_t). Это **pointer arithmetic** — типичная C-идиома. Условие: `i < tess.numVertexes` — пока не прошли все вершины.

```c
        s = DotProduct( v, fogDistanceVector ) + fogDistanceVector[3];
        t = DotProduct( v, fogDepthVector ) + fogDepthVector[3];
```

Вот **самое главное**: для каждой вершины считаем `s` и `t` — её координаты в fog ramp texture.

- `s` = «расстояние вершины вдоль view direction», масштабированное на `tcScale`. То есть «насколько глубоко эта вершина в fog'е вдоль взгляда». Чем дальше — тем больше `s` — тем туманнее (если fog texture линейно идёт от прозрачного к непрозрачному по s-оси).
- `t` = «расстояние вершины над fog plane». Используется только когда камера снаружи fog volume — даёт правильный fade на границе.

```c
        if ( eyeOutside ) {
            if ( t < 1.0 ) {
                t = 1.0 / 32;
            } else {
                t = 1.0 / 32 + 30.0 / 32 * t / ( t - eyeT );
            }
        } else {
            if ( t < 0 ) {
                t = 1.0 / 32;
            } else {
                t = 31.0 / 32;
            }
        }
```

**Это раскладка четырёх случаев** в зависимости от:
- камера снаружи vs внутри fog volume
- вершина над fog plane vs под

Чтобы выбрать правильный участок fog ramp текстуры (`t` это координата по второй оси). Магические числа `1/32` и `31/32` — это «у самого края текстуры, но не на нём» — чтобы фильтрация GPU не подцепила пиксели за границей. Та же логика что и `1/512` выше.

Формула `1/32 + 30/32 * t / (t - eyeT)` — **интерполяция в перспективе** между «вершина у пола fog» и «вершина у границы». Когда камера снаружи и вершина внутри, нужно правильно интерполировать tex coord чтобы fog ramp shader'у как бы плавно «врубалась» на пересечении луча взгляда с плоскостью.

```c
        st[0] = s;
        st[1] = t;
        st += 2;
    }
}
```

Записываем посчитанные `s, t` в output массив, сдвигаем указатель на следующую пару — следующую вершину.

**Конец функции.** Что произошло: для каждой вершины поверхности мы посчитали пару (s, t) texture coords. Эти координаты дальше отправляются в shader stage где fog texture накладывается:
- GPU **интерполирует** (s, t) между вершинами для каждого пикселя.
- Для каждого пикселя берётся sample из fog texture по этим координатам.
- Цвет fog'а (из shader stage) смешивается с цветом поверхности используя альфу из fog texture.

Результат: поверхность плавно затумаривается с расстоянием, **без per-pixel вычислений** — всё посчитали один раз на вершину, GPU добил интерполяцией.

---

## Часть 3. Global fog (RTCW addition)

Эта система **независима** от distance fog. Она про «весь мир сейчас в таком тумане».

### glfog_t struct

`code/renderer/tr_types.h:223`:

```c
typedef struct {
    int mode;
    int hint;
    int startTime;
    int finishTime;
    float color[4];
    float start;
    float end;
    qboolean useEndForClip;
    float density;
    qboolean registered;
    qboolean drawsky;
    qboolean clearscreen;

    int dirty;
} glfog_t;
```

**Построчно:**

- `int mode;` — OpenGL fog mode: `GL_LINEAR` (линейный fade от `start` до `end`) или `GL_EXP` (экспоненциальный — туман нарастает по формуле `e^(-density*z)`). Линейный — управляемый художником диапазон; exp — реалистичнее, но менее предсказуем.
- `int hint;` — quality hint OpenGL'у: `GL_DONT_CARE`, `GL_FASTEST`, `GL_NICEST`. Просто рекомендация драйверу — большинство дров игнорят.
- `int startTime, finishTime;` — это **timing для transition**. Когда game logic меняет fog мгновенно — обоим присваивается одно. Когда хочется плавный переход за N секунд — finishTime = startTime + N*1000. Каждый кадр R_Fog проверяет: «прошло ли уже finishTime? если нет — интерполируем».
- `float color[4];` — RGBA цвет тумана. Альфа обычно 1.0 (не используется напрямую, но хранится).
- `float start;` — для линейного режима: с какого расстояния туман начинается. Ближе — чистый воздух.
- `float end;` — для линейного: на каком расстоянии 100% туман. Дальше — невидимо.
- `qboolean useEndForClip;` — флаг «использовать end value ещё и как far clipping plane». Если да — то всё что за fog'ом не рендерится вообще (экономия GPU).
- `float density;` — для экспоненциального режима: сила тумана. Чем больше — тем гуще.
- `qboolean registered;` — этот fog slot валидный (game выставил его), или пустой?
- `qboolean drawsky;` — рисовать ли skybox через туман или нет. В густом тумане skybox не виден; в редком — виден.
- `qboolean clearscreen;` — перед каждым кадром заливать экран цветом тумана. Создаёт эффект густой пелены.
- `int dirty;` — флаг «параметры изменились с прошлого кадра» — сигнал R_Fog перевалидировать.

### Множество fog slots

Из `tr_types.h:200`:
```c
typedef enum {
    FOG_NONE,           // 0 — нет fog'а
    FOG_SKY,            // 1 — для skybox'а
    FOG_PORTALVIEW,     // 2 — для portal камер (mirror'ы, screens)
    FOG_HUD,            // 3 — для 3D-моделей в HUD (оружие, инвентарь)
    FOG_MAP,            // 4 — fog заданный в .shader sky brushа
    FOG_WATER,          // 5 — fog когда под водой
    FOG_SERVER,         // 6 — fog который game-сервер выставил (target_fog entity)
    FOG_CURRENT,        // 7 — текущее активное состояние
    FOG_LAST,           // 8 — состояние перед текущим transition
    FOG_TARGET,         // 9 — состояние которое мы interполируем В
    FOG_CMD_SWITCHFOG,  // 10 — команда «переключи на fog Y за N миллисекунд»
    NUM_FOGS
} glfogType_t;
```

**Слотов 11.** Это не значит «11 туманов одновременно». Каждый слот — это **назначение**:

- `FOG_HUD`, `FOG_PORTALVIEW`, `FOG_WATER` — специализированные для разных «scenes» внутри одного кадра. У 3D-вьюшки оружия в углу свой fog. У зеркала свой. У основного мира свой.
- `FOG_MAP`, `FOG_SERVER` — **источники** «откуда пришёл fog в основной мир». MAP — из конфига карты, SERVER — от gameplay.
- `FOG_CURRENT, FOG_LAST, FOG_TARGET` — это **state machine** transition'а. Когда происходит «плавно поменяй fog»:
  - Состояние ДО transition'а копируется в `FOG_LAST`.
  - Состояние ПОСЛЕ копируется в `FOG_TARGET`.
  - `FOG_CURRENT` — то что сейчас на экране (lerp'ится между LAST и TARGET по времени).

Это глобальный массив:
```c
glfog_t glfogsettings[NUM_FOGS];
```

11 копий структуры `glfog_t`. Очень flexible, но и сложно.

### R_SetFog — game зовёт когда хочет поменять fog

`code/renderer/tr_main.c:199` — функция принимает 7 параметров и **работает в двух режимах**.

**Параметры:**
- `int fogvar` — какой слот fog'а трогаем (FOG_MAP / FOG_SERVER / FOG_PORTALVIEW / FOG_CMD_SWITCHFOG)
- `int var1, int var2` — смысл зависит от режима
- `float r, g, b` — цвет
- `float density` — плотность

**Режим 1: установить параметры в слот.**

```c
if ( fogvar != FOG_CMD_SWITCHFOG ) {
    if ( var1 == 0 && var2 == 0 ) {
        glfogsettings[fogvar].registered = qfalse;
        return;
    }
```

Если оба var нулевые — **очищаем** этот слот (registered=false). Это способ «убери fog в FOG_MAP».

```c
    glfogsettings[fogvar].color[0]      = r;
    glfogsettings[fogvar].color[1]      = g;
    glfogsettings[fogvar].color[2]      = b;
    glfogsettings[fogvar].color[3]      = 1;
    glfogsettings[fogvar].start         = var1;
    glfogsettings[fogvar].end           = var2;
```

Записываем цвет в RGBA (альфа всегда 1) и near/far range из var1/var2.

```c
    if ( density >= 1 ) {
        glfogsettings[fogvar].mode          = GL_LINEAR;
        glfogsettings[fogvar].drawsky       = qfalse;
        glfogsettings[fogvar].clearscreen   = qtrue;
        glfogsettings[fogvar].density       = 1.0;
    } else {
        glfogsettings[fogvar].mode          = GL_EXP;
        glfogsettings[fogvar].drawsky       = qtrue;
        glfogsettings[fogvar].clearscreen   = qfalse;
        glfogsettings[fogvar].density       = density;
    }
```

Хитрая конвенция: **density ≥ 1** означает «художник хочет линейный fog с указанным near/far», поэтому ставим LINEAR, выключаем небо (густой fog), очищаем экран цветом. **density < 1** означает «exponential fog с указанной плотностью», небо рисуем (можно увидеть через дымку), экран не очищаем (туман строится постепенно).

```c
    glfogsettings[fogvar].hint          = GL_DONT_CARE;
    glfogsettings[fogvar].registered    = qtrue;
    return;
}
```

Финал режима 1: помечаем slot как валидный и выходим.

**Режим 2: команда переключения** (когда `fogvar == FOG_CMD_SWITCHFOG`):

```c
if ( var1 == FOG_MAP ) {
    if ( glfogsettings[FOG_CURRENT].registered ) {
        memcpy( &glfogsettings[FOG_LAST], &glfogsettings[FOG_CURRENT], sizeof( glfog_t ) );
    }
    memcpy( &glfogsettings[FOG_TARGET], &glfogsettings[glfogNum], sizeof( glfog_t ) );
    memset( &glfogsettings[FOG_MAP], 0, sizeof( glfog_t ) );
    memset( &glfogsettings[FOG_TARGET], 0, sizeof( glfog_t ) );
    glfogNum = FOG_NONE;
    return;
}
```

**Что такое `memcpy` и `memset`?**
- `memcpy(dst, src, size)` — копировать `size` байт из `src` в `dst`. Здесь копирует целую `glfog_t` структуру.
- `memset(dst, value, size)` — забить `size` байт значением `value`. С value=0 — обнуляет.
- `sizeof(glfog_t)` — компиляторская константа: «сколько байт занимает структура `glfog_t`».

`&` оператор — «адрес чего-то» (берём указатель на struct в массиве).

**Что происходит:** «выключить весь fog».
1. Если CURRENT есть — сохраняем как LAST (откуда транзит начнётся).
2. Скопировали глобально активный fog в TARGET.
3. Обнулили MAP и TARGET — это финальные значения «никакого fog'а».
4. `glfogNum = FOG_NONE` — забыли «который fog сейчас активен».

Дальше — нормальная команда «switch to fog X»:

```c
if ( glfogsettings[var1].registered != qtrue ) {
    return;
}

glfogNum = var1;

if ( glfogsettings[FOG_CURRENT].registered ) {
    memcpy( &glfogsettings[FOG_LAST], &glfogsettings[FOG_CURRENT], sizeof( glfog_t ) );
} else {
    memcpy( &glfogsettings[FOG_LAST], &glfogsettings[glfogNum], sizeof( glfog_t ) );
}

memcpy( &glfogsettings[FOG_TARGET], &glfogsettings[glfogNum], sizeof( glfog_t ) );
```

1. Проверяем что fog в который переходим существует (зарегистрирован).
2. Запоминаем номер активного fog'а.
3. **Откуда идём** (LAST): из CURRENT если он есть; иначе из самого target'а (значит «не было transition'а — начинаем мгновенно»).
4. **Куда идём** (TARGET): новые параметры.

```c
if ( !var2 ) {
    glfogsettings[FOG_TARGET].startTime = 0;
    glfogsettings[FOG_TARGET].finishTime = 0;
    glfogsettings[FOG_TARGET].dirty = 1;
    glfogsettings[FOG_CURRENT].dirty = 1;
    ...
}
```

Если `var2 = 0` — **мгновенный switch без анимации**. Иначе (не показано в моём cut) — выставляются startTime/finishTime = milliseconds для transition.

### R_Fog — каждый кадр перед рисованием

`tr_main.c:69`. Вызывается **один раз в начале каждого кадра** и говорит OpenGL «настройся на такой fog».

```c
void R_Fog( glfog_t *curfog ) {
    if ( !r_wolffog->integer ) {
        R_FogOff();
        return;
    }
```

`r_wolffog` это cvar (console variable) — игрок может в консоли выключить fog через `\r_wolffog 0`. Полезно для скриншотов или дебага. Если 0 — выключаем GL fog state и выходим.

```c
    if ( !curfog->registered ) {
        R_FogOff();
        return;
    }
```

Если переданный fog не зарегистрирован (NULL-state) — выключаем.

```c
    if ( !curfog->density ) {
        curfog->density = 1;
    }
    if ( !curfog->hint ) {
        curfog->hint = GL_DONT_CARE;
    }
    if ( !curfog->mode ) {
        curfog->mode = GL_LINEAR;
    }
```

Дефолты для случаев когда game не заполнил (нули).

```c
    R_FogOn();

    qglFogi( GL_FOG_MODE, curfog->mode );
    qglFogfv( GL_FOG_COLOR, curfog->color );
    qglFogf( GL_FOG_DENSITY, curfog->density );
    qglHint( GL_FOG_HINT, curfog->hint );
```

**Это самое OpenGL-specific место.** Функции `qglFog*` это обёртки вокруг родных OpenGL `glFog*` (тонкий слой для динамической загрузки):

- `glFogi(GL_FOG_MODE, ...)` — установить fog mode (LINEAR или EXP). `i` в конце = `integer` parameter.
- `glFogfv(GL_FOG_COLOR, ...)` — установить fog color. `fv` = `float vector` — принимает указатель на массив из 4 float'ов (RGBA).
- `glFogf(GL_FOG_DENSITY, ...)` — установить density. `f` = `float`.
- `glHint(GL_FOG_HINT, ...)` — quality hint.

Это **fixed-function pipeline** OpenGL. GPU при рендере **сам по себе** вычисляет fog для каждого пикселя — нам не надо писать shader. **Этой механики в Vulkan не существует.** В Vulkan надо явно делать всё это в fragment shader'е.

```c
    if ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) {
        qglFogf( GL_FOG_START, curfog->end );
    } else {
        qglFogf( GL_FOG_START, curfog->start );
    }
```

`RDF_SNOOPERVIEW` это **флаг** что сейчас рисуется камера снайперского прицела. **Что значит `&`?** Здесь это **битовая операция AND**. `rdflags` это битовая маска: каждый бит — отдельный флаг. `rdflags & RDF_SNOOPERVIEW` оставляет только тот бит, отбрасывая остальные. Результат не-ноль если бит установлен.

Через прицел туман отодвигается дальше (`start = end` — туман начинается там где обычно заканчивался). Логично: снайпер видит сквозь дымку лучше чем простой солдат.

```c
    if ( r_zfar->value ) {
        qglFogf( GL_FOG_END, r_zfar->value );
    } else {
        ...
        qglFogf( GL_FOG_END, curfog->end );
    }
```

`r_zfar` — debug cvar для level designer'ов: переопределить far value для тестирования. Иначе берём из fog данных.

```c
    qglClearColor( curfog->color[0], curfog->color[1], curfog->color[2], curfog->color[3] );
}
```

Устанавливаем GL clear color = цвет fog. Когда позже будет `glClear(GL_COLOR_BUFFER_BIT)`, экран зальётся этим цветом — что создаёт эффект «горизонт сливается с туманом».

### R_FogOn / R_FogOff

```c
void R_FogOff( void ) {
    if ( !fogIsOn ) {
        return;
    }
    qglDisable( GL_FOG );
    fogIsOn = qfalse;
}
```

`qglDisable(GL_FOG)` — выключить fog state в OpenGL. `glEnable/glDisable(GL_FOG)` это OpenGL toggle: после `Enable` каждый рисуемый пиксель будет с fog. Флаг `fogIsOn` — наша оптимизация чтобы не дёргать GL зря.

---

## Часть 4. Что значит «портировать fog» на Vulkan

**Distance fog** (`RB_CalcFogTexCoords`):
- Вся математика **language-neutral** — copy-paste в renderervk файл.
- Никаких OpenGL вызовов в самой функции — она только заполняет `st[]` массив.
- Shader stage который потом применяет fog texture к surface — уже умеет Q3e Vulkan renderer (там же базовая Q3 fog system которая совпадает).
- **Лёгкая часть. Полдня работы.**

**Global fog** (`R_SetFog`, `R_Fog`):
- `R_SetFog` — чистая state-machine логика, language-neutral. Copy-paste.
- `R_Fog` — **тут проблема**. Все `qglFog*` calls и `glEnable(GL_FOG)` — это OpenGL fixed-function pipeline. **Vulkan не имеет fixed-function fog**. Совсем. Никакого `vkFogEnable` нет.
- Что в Vulkan делается:
  1. Создаётся **uniform buffer** в котором лежат fog parameters (mode, color, start, end, density).
  2. Каждый кадр R_Fog обновляет этот buffer вместо `qglFog*`.
  3. **Fragment shader** для каждого пикселя сам считает fog factor: «если LINEAR mode: `factor = clamp((end - z) / (end - start), 0, 1)`; если EXP mode: `factor = exp(-density * z)`».
  4. В shader'е финальный цвет = `mix(fogColor, surfaceColor, factor)`.

То есть **fixed-function код переписывается в shader'ные uniforms + GLSL formula**. У Q3e Vulkan renderer'а **скорее всего уже есть** базовая fog в shader'ах (потому что Q3 fog тоже использовал fixed-function в эпоху GL 1.x). Но надо проверить покрывает ли его реализация **обе режима** (LINEAR + EXP) и поддерживает ли transition с интерполяцией.

- Transition state machine (`memcpy(&FOG_LAST, &FOG_CURRENT, ...)`, lerp по времени) — портируется как есть.
- `clearColor` — в Vulkan это часть render pass setup (`VkClearValue` при `vkCmdBeginRenderPass`). Эквивалент `glClearColor` есть, просто другая форма.

**Cредняя часть.** Один-два дня работы.

---

## Часть 5. Итого по объёму fog портирования

| Кусок | Сложность | Время |
|---|---|---|
| `fogParms_t`, `fog_t`, `glfog_t` структуры | Тривиально (copy) | 30 мин |
| Чтение fog brushes из BSP | Уже работает в Q3e | 0 (проверить) |
| `RB_CalcFogTexCoords` (per-vertex math) | Copy-paste | 2 часа |
| Shader stage накладывающий fog texture | Q3e уже имеет (проверить) | 0-полдня |
| `R_SetFog` state machine | Copy-paste | 1 час |
| `R_Fog` GL→Vulkan переписать | Среднее | 1-2 дня |
| `R_FogOn/Off` | Удалить (Vulkan не нужен toggle) | 10 мин |
| Transition lerp | Copy-paste | 30 мин |
| Тестирование на катакомбах + улицах | Глазами | полдня |

**Итого: 3-4 рабочих дня для full fog parity.** Самое трудоёмкое — переписать fixed-function `R_Fog` на shader uniforms.

---

## Что в этом коде неочевидно но важно

1. **fog distance считается в model-space**, не world-space. Поэтому вся возня с `backEnd.or.modelMatrix` и `axis[]` в начале `RB_CalcFogTexCoords`. Когда рисуется модель монстра — её локальная система координат не совпадает с миром, и fog надо «повернуть» в её систему. Если забыть — туман будет вращаться вокруг монстра.

2. **Texture coordinate trick** в distance fog — это эпохальная оптимизация Quake 3 эпохи. Сейчас (с программируемыми shader'ами) проще считать fog per-pixel прямо в shader'е. Q3e Vulkan может использовать оба подхода — посмотрим что выбрали.

3. **Два fog'а могут наложиться:** глобальный (`glfogsettings[FOG_CURRENT]`) и brush (`tess.fogNum`). Если ты в катакомбах под открытым небом с дождём — оба активны. Их применение поэтапное: вершины окрашиваются brush fog'ом, потом весь кадр перерисовывается с global fog. Это **порядок операций**, не суммирование цветов.

4. **`r_wolffog` cvar выключает только глобальный fog**. Brush fog продолжает работать. Странность — но логика такая что brush fog это «часть геометрии уровня», художник так задумал, игрок не должен это отключать. А global fog — это «атмосфера», можно убрать для скриншотов.

5. **`drawsky` флаг важен для open-world уровней.** Если `drawsky=false` (густой fog) — skybox не рисуется вообще, экономия. RTCW тщательно различает «густой туман в катакомбах — skybox не нужен» от «лёгкая дымка снаружи — skybox через неё виден».
