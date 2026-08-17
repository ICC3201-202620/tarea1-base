# Tarea 1 — Procesos visibles y planificación en xv6

**ICC3201 — Sistemas Operativos y Redes · semestre 2026-20**

En esta tarea trabajarás directamente con un kernel real: xv6, una adaptación
para x86 de Unix versión 6 (1975). El Unix original fue escrito en C para el
PDP-11; su diseño pequeño y su código legible hacen posible estudiar en
concreto cómo un sistema operativo implementa abstracciones como procesos,
llamadas al sistema y planificación.

La tarea tiene dos partes conectadas. En la primera construirás una herramienta
de usuario similar a `ps`, que obtiene una instantánea de los procesos desde el
kernel. En la segunda modificarás el planificador para observar cómo cambian
esas mediciones.

## Modalidad y entrega

La tarea se resuelve **en parejas**. Los grupos deben estar inscritos mediante
el formulario difundido por el curso. Para la evaluación se considerará el
último commit anterior al plazo de entrega:

> **Viernes 11 de septiembre, 23:59 hrs.**

La entrega debe compilar y ejecutarse en el código base entregado. No se deben
modificar los programas de prueba ni el mecanismo de compilación para evitar
la evaluación de la funcionalidad solicitada.

### Repositorios y remotos Git

GitHub Classroom ya no está disponible. Mientras se procesan las inscripciones
y el curso crea los repositorios definitivos bajo la organización, cada grupo
debe trabajar de la siguiente forma:

Antes de crear el primer commit, cada integrante debe configurar su identidad
en Git. Usa un correo asociado a tu cuenta de GitHub (puede ser el correo
privado `noreply` de GitHub si lo prefieres):

```sh
git config --global user.name "Nombre Apellido"
git config --global user.email "correo-asociado-a-github@ejemplo.com"
```

Puedes revisar la configuración efectiva con:

```sh
git config --get user.name
git config --get user.email
```

1. Una persona del grupo clona este repositorio y crea un **repositorio privado
   temporal** en su cuenta de GitHub.
2. Esa persona invita al otro integrante del grupo al repositorio privado.
3. En los clones de ambos integrantes, configuren los remotos con el mismo
   significado:
   - `origin`: repositorio privado temporal del grupo; se usa para hacer
     *push* mientras no exista el repositorio definitivo.
   - `upstream`: este repositorio de código base del curso; se usa para
     obtener actualizaciones que el equipo docente publique.

Un modo de obtener esta configuración inicial, después de clonar el código
base, es:

```sh
git remote rename origin upstream
git remote add origin git@github.com:USUARIO/TAREA1-PRIVADA.git
git push -u origin main
```

El nombre y la URL del repositorio privado son sólo un ejemplo; lo importante
es que sea privado, que ambos integrantes tengan acceso y que `origin` sea el
destino de los *push*.

Posteriormente el equipo docente creará un repositorio por grupo bajo la
organización `ICC3201-202620` e invitará a sus integrantes. Al recibir la
invitación, cada integrante debe cambiar `origin` para que apunte al
repositorio asignado y continuar empujando allí:

```sh
git remote set-url origin git@github.com:ICC3201-202620/REPOSITORIO-ASIGNADO.git
git push -u origin main
```

`upstream` debe seguir apuntando al código base del curso. Desde ese momento,
la entrega oficial es el repositorio asignado bajo la organización; el
repositorio privado temporal puede conservarse sólo como respaldo.

## Antes de comenzar

### Material de apoyo

El texto del curso es [Operating Systems: Three Easy Pieces (OSTEP)](https://pages.cs.wisc.edu/~remzi/OSTEP/).
Para esta parte son especialmente pertinentes los capítulos sobre virtualización
de CPU, procesos, llamadas al sistema y planificación.

También puedes revisar esta [playlist de YouTube sobre xv6 y kernel hacking](https://youtube.com/playlist?list=PL3yryPU8iwGO2IsoEa_F8_zIytuHIHV37).
Los primeros videos explican cómo orientarse en el código y cómo se implementa
una syscall; el video sobre planificación y sincronización será útil también
para la segunda parte.

Al enfrentarte a un código base grande, busca primero implementaciones
parecidas. Por ejemplo, estudia `getpid()` para seguir el recorrido completo de
una syscall, desde el programa de usuario hasta el kernel. Lee la
implementación de `wait()` y el uso de `ptable.lock` para entender cómo se
recorre de manera coherente la tabla de procesos.

### Compilar, ejecutar y depurar

xv6 requiere un compilador que genere ejecutables ELF i386 de 32 bits y QEMU.
En Linux o WSL suele bastar con usar las herramientas nativas; en ese caso deja
vacío `TOOLPREFIX` en el `Makefile`:

```make
TOOLPREFIX =
```

En macOS se puede instalar el compilador cruzado con Homebrew:

```sh
brew install i686-elf-gcc
```

Luego ajusta `TOOLPREFIX` en el `Makefile` para que coincida con el prefijo
instalado, normalmente:

```make
TOOLPREFIX = i686-elf-
```

Compila e inicia xv6 con:

```sh
make qemu-nox
```

Para salir de QEMU usa `Ctrl-a x`. Para depurar, inicia xv6 en modo GDB con:

```sh
make qemu-nox-gdb
```

Y, en otra terminal, ejecuta:

```sh
gdb kernel/kernel
```

Dentro del kernel puedes usar `cprintf()` para depurar. Ante una condición
irrecuperable, `panic()` imprime información de depuración y detiene xv6.

# Parte 1 — Una syscall para `ps` (40%)

## Objetivo

Implementarás una nueva syscall que entrega al espacio de usuario una
instantánea de la tabla de procesos. Sobre ella construirás un programa de
usuario llamado `ps`, inspirado en la herramienta homónima de Unix.

El propósito no es replicar todas las opciones de `ps`, sino comprender:

- cómo se representa un proceso mediante su PCB (`struct proc`);
- cómo se conecta una syscall entre espacio de usuario y kernel;
- cómo se valida y llena un arreglo que pertenece al espacio de usuario;
- cómo se protege la tabla de procesos mediante locks; y
- cómo se contabilizan el tiempo de CPU y el tiempo esperando procesador.

## Interfaz requerida

El código base ya incluye `kernel/procinfo.h`, un header visible tanto para el
kernel como para programas de usuario; el `Makefile` agrega `kernel/` a la ruta
de inclusión de ambos. Este header define la interfaz pública requerida:

```c
#define PROC_NAME_LEN 16

struct procinfo {
  int pid;                     // identificador del proceso
  int ppid;                    // PID del padre; 0 si no existe
  int state;                   // estado del proceso
  int sz;                      // memoria de usuario, en bytes
  int rtime;                   // ticks acumulados en RUNNING
  int wtime;                   // ticks acumulados en RUNNABLE
  int priority;                // nivel MLFQ: 0 (alta) o 1 (baja)
  char name[PROC_NAME_LEN];    // nombre del proceso
};
```

No elimines ni cambies el significado de esos campos. El header también define
las constantes públicas `PSTATE_UNUSED`, `PSTATE_EMBRYO`, `PSTATE_SLEEPING`,
`PSTATE_RUNNABLE`, `PSTATE_RUNNING` y `PSTATE_ZOMBIE`, cuyos valores respetan
el orden de los estados internos de xv6. El campo `priority` se usará en la
Parte 2; no es necesario completarlo para la Parte 1.

La syscall tendrá la siguiente firma:

```c
int getprocs(struct procinfo *buf, int max);
```

`buf` apunta a un arreglo en espacio de usuario con capacidad para `max`
entradas. La syscall copia una entrada por cada proceso cuyo estado no sea
`UNUSED`, hasta llenar el arreglo, y retorna la cantidad de entradas copiadas.

Sus casos límite son parte de la especificación:

- Si `max == 0`, no copia entradas y retorna `0`.
- Si `max < 0`, o si el rango de memoria indicado por `buf` no es válido para
  el proceso llamador, retorna `-1`.
- Si existen más procesos que cupos, se retorna `max`; no es un error.
- El orden de las entradas no importa.

La información es una **instantánea**: puede cambiar inmediatamente después
de que la syscall retorne. No es necesario congelar los procesos, pero el
recorrido y la extracción de cada entrada deben realizarse de forma coherente
bajo el lock apropiado.

## Contabilidad de tiempos

Extiende `struct proc` con los campos `rtime` y `wtime`:

```c
int rtime;  // ticks totales en estado RUNNING
int wtime;  // ticks totales en estado RUNNABLE
```

Inicialízalos en `allocproc()`. Cada interrupción de timer debe incrementar
`rtime` de los procesos que se encuentren en `RUNNING` y `wtime` de los que se
encuentren en `RUNNABLE`. Puedes implementar un helper para esta contabilidad
y llamarlo desde el manejador de timer en `kernel/trap.c`.

Un tick dura aproximadamente 10 ms en esta configuración de xv6. Los valores
deben mostrarse como ticks: no debes convertirlos a milisegundos. El tiempo se
acumula desde la creación del proceso; un hijo creado por `fork()` comienza con
contadores en cero.

Toda lectura o modificación de campos de un proceso durante este recorrido
debe respetar `ptable.lock`. En particular, no introduzcas un orden de locks
que pueda interferir con el manejo de interrupciones de timer.

## Implementación de `getprocs()`

Para integrar la syscall debes completar todas las capas habituales de xv6:

1. Implementar la función de kernel que recorre `ptable.proc` y llena las
   estructuras `procinfo`.
2. Implementar `sys_getprocs()` en `kernel/sysproc.c`, obteniendo el puntero y
   el entero desde los argumentos de la syscall.
3. Reservar un número no usado en `kernel/syscall.h`.
4. Agregar el wrapper a `kernel/syscall.c` y la entrada correspondiente en la
   tabla `syscalls[]`.
5. Declarar el prototipo en `user/user.h` y agregar `SYSCALL(getprocs)` a
   `kernel/usys.S`.

La validación de `buf` debe cubrir **todo** el arreglo solicitado, no sólo su
primera entrada. Considera también el caso de `max` negativo y evita que un
cálculo de tamaño inválido permita escribir fuera de la memoria del proceso.

No expongas un puntero a `struct proc` ni copies la estructura interna completa
al espacio de usuario: `procinfo` es la interfaz pública y deliberadamente
reducida.

## Programa de usuario `ps`

Implementa `user/ps.c` y agrégalo a `UPROGS` en el `Makefile`. Al ejecutarse,
debe invocar a `getprocs()` y mostrar una tabla legible, por ejemplo:

```text
PID   PPID  STATE     CPU  WAIT  NAME
1     0     SLEEPING  3    0     init
2     1     RUNNING   8    2     sh
```

`STATE` debe imprimirse como texto, no como un número. El encabezado puede
tener distinto espaciado, pero la salida debe contener todos los campos
requeridos. El programa debe manejar un retorno `-1` de la syscall e informar
un error comprensible.

El código base incluye `user/ps_test.c`. Este programa prueba los casos
`max == 0` y `max < 0`; crea un hijo CPU-bound y otro que se duerme; y comprueba
que una instantánea contenga procesos vivos con estados y contadores válidos.
Debes usarlo para validar tu solución, pero no debes modificarlo.

Una vez implementada la syscall y sus enlaces, descomenta la entrada
`$U/_ps_test` del `Makefile`, recompila xv6 y ejecútalo desde su shell:

```sh
$ ps_test
ps_test: PASS
```

El test no reemplaza tus propias pruebas ni los casos privados de evaluación.

## Criterios de evaluación de la Parte 1

Cada parte se evaluará globalmente en una escala de logro de 1 a 5. Para la
Parte 1 se considerará en conjunto la implementación de `getprocs()`, la
contabilidad de tiempos, la transferencia segura de datos, el programa `ps`,
el manejo de locks y casos límite, las pruebas y la calidad del código.

| Nivel | Descripción |
| ---: | --- |
| 1 | No entregado. |
| 2 | Esbozo de solución. |
| 3 | Implementación parcial y/o defectuosa. |
| 4 | Solución con faltas o errores menores. |
| 5 | Funcional y correcta. |

Las dos partes tienen el mismo peso: hasta **3,0 puntos de nota** cada una. La
nota final se calculará como:

```text
nota final = 1,0 + 3,0 × logro(Parte 1) + 3,0 × logro(Parte 2)
```

donde `logro` transforma el nivel de la tabla de la siguiente manera:

| Nivel | `logro` |
| ---: | ---: |
| 1 | 0 |
| 2 | 0,25 |
| 3 | 0,50 |
| 4 | 0,75 |
| 5 | 1 |

Así, una Parte 1 completamente lograda y una Parte 2 no entregada dan una nota
de 4,0. Para alcanzar una nota alta es necesario un avance sustancial en ambas
partes.

## Parte 2 — Planificación de procesos

### Objetivo

Modificarás el scheduler de xv6 para implementar una versión pedagógica y
deliberadamente simplificada de **Multi-Level Feedback Queue (MLFQ)**. Esta
política favorece a los procesos interactivos —que alternan ráfagas breves de
CPU con espera por E/S— por sobre procesos que consumen CPU continuamente.

No se pide implementar un MLFQ canónico ni construir colas enlazadas. El foco
es entender cuándo se toma una decisión de planificación, cómo cambian los
estados de un proceso y qué consecuencias tienen las prioridades sobre el uso
de CPU y el tiempo de espera.

Para eliminar complejidad ajena al objetivo, esta tarea se ejecutará con un
solo procesador virtual. El código base fija `CPUS := 1`; no cambies esa
configuración ni evalúes tu implementación con más CPUs.

### Política requerida

Implementa dos niveles de prioridad:

| Nivel | Prioridad | Quantum | Comportamiento esperado |
| ---: | --- | ---: | --- |
| 0 | alta | 1 tick | procesos nuevos o que acaban de despertar |
| 1 | baja | 3 ticks | procesos CPU-bound que ya consumieron CPU en nivel 0 |

Un número menor representa una prioridad mayor. Define constantes con nombres
claros, por ejemplo `MLFQ_HIGH`, `MLFQ_LOW`, `QUANTUM_HIGH` y `QUANTUM_LOW`.
Evita dispersar valores mágicos en el código.

La política debe cumplir las siguientes reglas:

1. **Selección por prioridad.** En cada decisión de planificación se elige un
   proceso `RUNNABLE` de nivel 0 si existe alguno. Sólo si no hay procesos
   elegibles en nivel 0 se selecciona uno de nivel 1.
2. **Round-robin dentro de un nivel.** Los procesos `RUNNABLE` de un mismo
   nivel deben turnarse; no basta recorrer siempre la tabla desde su primera
   entrada. Mantén un cursor o índice por nivel para iniciar la siguiente
   búsqueda después del último proceso seleccionado.
3. **Democión.** Todo proceso nuevo comienza en nivel 0. Cuando ejecuta el
   quantum completo de nivel 0, baja a nivel 1 y reinicia su contador. Al
   consumir un quantum completo en nivel 1, permanece allí y reinicia ese
   contador.
4. **Promoción al despertar.** Cuando un proceso pasa de `SLEEPING` a
   `RUNNABLE`, vuelve a nivel 0 y reinicia el contador.
5. **Boost global.** Cada 50 ticks de timer, todos los procesos que no estén
   `UNUSED` vuelven a nivel 0 y reinician su contador. El boost evita la
   inanición permanente de procesos de nivel 1.

El contador usado para medir el quantum es independiente de `rtime`: éste
continúa acumulando todos los ticks de CPU desde la creación del proceso,
mientras que el contador del quantum se reinicia en cada cambio de nivel,
promoción o boost.

### Datos y lugares de modificación

El código base ya declara en `struct proc` los campos mínimos de apoyo:

```c
int priority;       // MLFQ_HIGH o MLFQ_LOW
int slice_ticks;    // ticks consumidos en el quantum vigente
```

`allocproc()` ya los inicializa para que los procesos nuevos —incluidos los
hijos creados con `fork()`— comiencen en nivel 0. Debes completar las
actualizaciones posteriores: democión, promoción y boost.

Debes revisar principalmente:

- `kernel/proc.c`: inicialización, selección en `scheduler()`, round-robin y
  promoción en el camino de `wakeup`;
- `kernel/trap.c`: actualización del quantum y boost desde la interrupción de
  timer, antes del `yield()` ya provisto por xv6;
- `kernel/proc.h`: campos y constantes de planificación; y
- `Makefile`: habilitación de `schedtest` en `UPROGS`.

La interrupción de timer ya provoca que el proceso corriente ceda la CPU tras
cada tick. Por ello, un quantum de tres ticks se logra seleccionando otra vez
al mismo proceso de nivel 1 mientras siga siendo el candidato round-robin; no
debes cambiar el mecanismo de planificación expropiativa de xv6.

El scheduler y las transiciones de estado deben respetar `ptable.lock`.
Mantén ese lock tomado al seleccionar el nivel, elegir el proceso y cambiar su
estado a `RUNNING`. No tomes `ptable.lock` mientras mantengas `tickslock`: el
manejador de timer ya separa esas regiones críticas y tu código debe conservar
esa separación.

### Hacer observable la política

El código base ya amplía `struct procinfo` de la Parte 1 con:

```c
int priority;       // nivel MLFQ actual: 0 (alta) o 1 (baja)
```

Actualiza `getprocs()` y `ps` para copiar e imprimir esta columna. Así puedes
inspeccionar los efectos del planificador sin introducir una syscall adicional.

El código base incluye `schedtest`, un programa de usuario que genera dos
procesos CPU-bound y uno interactivo que alterna trabajo breve con `sleep()`.
Cuando tengas implementadas ambas partes, descomenta `$U/_schedtest` en el
`Makefile`, recompila y ejecútalo junto con `ps`. Verifica, como mínimo, que:

- un proceso CPU-bound es degradado a nivel 1;
- un proceso que despierta vuelve a nivel 0;
- procesos del mismo nivel se alternan; y
- después de un boost los procesos vuelven a nivel 0.

No se calificará una proporción exacta de CPU: QEMU, la instrumentación y las
cargas de E/S hacen esas mediciones poco estables. Se calificará la política
observable, las transiciones requeridas y la ausencia de inanición evidente.

### Criterios de evaluación de la Parte 2

Esta parte usa la misma escala de logro 1–5 y aporta hasta 3,0 puntos a la
nota final, según la fórmula de la Parte 1. Se considerará en conjunto:

- selección estricta por prioridad y round-robin dentro de cada nivel;
- democión por consumo de quantum, promoción al despertar y boost global;
- inicialización y actualización coherente de los campos de planificación;
- uso correcto de `ptable.lock` y ejecución con un CPU;
- extensión de `ps`/`getprocs()` y uso de `schedtest` para demostrar la
  política; y
- claridad, acotación y calidad del código.

Una solución de nivel 5 cumple todas las reglas de la política y las demuestra
con las pruebas entregadas. Una solución que sólo prioriza siempre el nivel 0,
que no realiza round-robin o que omite las transiciones de prioridad no cumple
la política completa.

## Uso de IA generativa y evaluación individual

Esta es una sección **común a toda la tarea**: no se debe repetir por separado
para cada parte. De acuerdo con la sección *Tareas del curso y uso de IA* del
syllabus, el uso de herramientas de IA generativa es permitido y constituye
una competencia profesional relevante. No obstante, no exime a ningún
integrante de comprender, justificar y mantener el código producido.

La entrega debe incluir un único `INFORME.md` con una sección llamada **Uso de
LLMs**, incluso si no se usaron. Sigue además el protocolo de declaración de
uso de IA definido por la Facultad. Como mínimo, indica:

- herramienta y/o modelo aproximado (p.ej., Claude Sonnet u Opus 4.8, GPT 5.6, etc.);
- para qué se utilizó;

Declarar un uso responsable no reduce la nota grupal. Sin embargo, la
responsabilidad por el código es individual además de grupal. Para verificar
esa comprensión, en la **Prueba 2** cada integrante responderá un ítem de
evaluación teórica de la tarea (IET): un ejercicio de revisión y explicación
de código de tipo *code review*, basado en lo entregado.

El IET tiene un máximo de 10 puntos, forma parte de la Prueba 2 y determina el
ponderador individual que se aplicará a la nota grupal de la tarea, según la
política descrita en el syllabus. Un desempeño suficiente en el IET mantiene
la nota grupal; un dominio incompleto la reduce proporcionalmente y un
desempeño destacado puede bonificarla, con tope de nota 7,0.
