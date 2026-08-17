# Pistas para trabajar con xv6

Este documento complementa el enunciado. Su propósito es ayudar a orientarse
en xv6 y evitar errores frecuentes; no reemplaza la lectura del código ni
describe una implementación completa de la tarea.

## Orientarse antes de modificar

En xv6 conviene seguir un camino existente antes de escribir código nuevo. Por
ejemplo, `getpid()` es una syscall pequeña: permite ver el prototipo de usuario,
el stub en ensamblador, el número de syscall, el dispatcher y la función
`sys_getpid()` en el kernel.

Para planificación, lee en este orden aproximado:

1. `struct proc` en `kernel/proc.h`;
2. `allocproc()`, `fork()`, `sleep()`, `wakeup1()`, `yield()` y `scheduler()`
   en `kernel/proc.c`; y
3. el caso `IRQ_TIMER` en `kernel/trap.c`.

No supongas que una variable se puede cambiar sólo porque está en `struct
proc`: primero identifica quién posee el lock que protege ese acceso.

## Por dónde empezar: Parte 1

Evita intentar implementar todo de una vez. Una secuencia de trabajo razonable
es la siguiente:

1. Lee `kernel/procinfo.h` y decide qué campo de `struct proc` origina cada
   dato que debe aparecer en la instantánea.
2. Sigue `getpid()` de punta a punta: `user/user.h`, `kernel/usys.S`,
   `kernel/syscall.h`, `kernel/syscall.c` y `kernel/sysproc.c`. Repite esas
   capas para declarar `getprocs()`. Compila en este punto: una syscall que aún
   devuelva un valor fijo permite detectar enlaces faltantes temprano.
3. Implementa la validación de argumentos en `sys_getprocs()`. Prueba primero
   los casos `max == 0`, `max < 0` y un puntero inválido.
4. Implementa el recorrido protegido de `ptable.proc` y la copia a
   `struct procinfo`.
5. Agrega los contadores de tiempo en `struct proc` y un helper llamado desde
   el timer. Comprueba con `ps` que cambian mientras hay actividad.
6. Implementa `user/ps.c`, habilita `ps_test` en `UPROGS` y ejecuta la prueba
   entregada dentro de xv6.

El orden no es obligatorio, pero separa problemas de enlace de syscall,
validación, concurrencia y presentación. Si algo falla, así resulta más fácil
ubicar la capa responsable.

## Por dónde empezar: Parte 2

La Parte 2 depende de que `getprocs()` y `ps` funcionen: primero conviene tener
una forma de observar procesos y sus contadores. Luego trabaja en incrementos
pequeños:

1. Lee por completo el `scheduler()` original y dibuja qué ocurre antes y
   después de `swtch()`. No modifiques todavía `switchuvm()`, `switchkvm()` ni
   `swtch()`.
2. Revisa los campos y constantes MLFQ ya incluidos en `kernel/proc.h`.
   Asegúrate de que los procesos nuevos inicien en prioridad alta y con
   `slice_ticks` en cero.
3. Cambia la selección del scheduler para preferir nivel 0 sobre nivel 1,
   inicialmente incluso antes de agregar round-robin. Verifica que xv6 siga
   iniciando.
4. Agrega un cursor por nivel y transforma la búsqueda en circular. Prueba con
   al menos dos procesos CPU-bound para confirmar que se alternan.
5. Desde la interrupción de timer, actualiza el consumo de quantum y aplica la
   democión. Mantén separado este contador de `rtime`.
6. Completa la promoción en `wakeup1()` y el boost periódico. Por último,
   muestra prioridad en `ps`, habilita `schedtest` y observa las transiciones.

Tras cada paso, vuelve a iniciar xv6. Un error en scheduler suele impedir que
aparezca el shell; cambios pequeños facilitan identificar qué transición de
estado o lock introdujo el problema.

## `ptable.lock` y la tabla de procesos

La tabla global `ptable.proc` contiene los PCB de todos los procesos. Su
spinlock, `ptable.lock`, protege el estado de cada entrada y las transiciones
entre estados. Una regla práctica es:

> Si recorres `ptable.proc` o lees/modificas campos de otro proceso, toma
> `ptable.lock`.

Un spinlock no pone procesos a dormir mientras espera: gira hasta obtener el
lock. Por eso las secciones críticas deben ser cortas; no hagas E/S ni trabajo
costoso mientras lo mantienes tomado.

`acquire(&ptable.lock)` y `release(&ptable.lock)` deben aparecer en pares en
todas las rutas de retorno. También es importante no llamar una función que
intente adquirir el mismo lock mientras ya lo sostienes, salvo que el código
de xv6 documente explícitamente ese caso.

`sleep(chan, lock)` es una excepción que merece lectura cuidadosa: adquiere
`ptable.lock` antes de soltar el lock recibido, marca al proceso como
`SLEEPING` y llama a `sched()`. Esta secuencia evita perder un `wakeup()` entre
la comprobación de una condición y el sueño.

## Estados de proceso

Los estados relevantes son:

```text
UNUSED -> EMBRYO -> RUNNABLE -> RUNNING
                         ^         |
                         |         v
                      SLEEPING <- ...
RUNNING -> ZOMBIE
```

No todas las flechas ocurren directamente, pero el diagrama ayuda a leer los
sitios que cambian `p->state`. Para la tarea importa especialmente distinguir:

- `RUNNING`: ocupa el CPU; y
- `RUNNABLE`: está listo, pero espera que el scheduler lo elija.

Un contador de CPU y un contador de espera no son sinónimos de tiempo desde la
creación: sólo aumentan mientras el proceso está en esos estados específicos.

## Interrupción de timer y `ticks`

La interrupción de reloj entra por `trap()` en `kernel/trap.c`. En la
configuración entregada, sólo CPU 0 incrementa la variable global `ticks`:

```c
acquire(&tickslock);
ticks++;
wakeup(&ticks);
release(&tickslock);
```

`tickslock` protege la variable global `ticks` y el canal usado por
`sleep(n)`. No protege la tabla de procesos. Si necesitas recorrer `ptable`
desde el timer, hazlo después de liberar `tickslock`, adquiriendo entonces
`ptable.lock`. Evitar tomar ambos locks simultáneamente previene órdenes de
locks difíciles de razonar.

Al final de un timer, xv6 llama a `yield()` si había un proceso en `RUNNING`.
Esto significa que el scheduler recibe una nueva oportunidad de elegir tras
casi cada tick. Es la base de la expropiación de xv6; no hace falta inventar
otro mecanismo de expropación de CPU para esta tarea.

## Qué cambian `switchuvm`, `switchkvm` y `swtch`

Estos tres nombres se parecen, pero operan en capas distintas. Por ahora basta
entenderlos como parte del protocolo ya provisto por xv6 para pasar entre el
scheduler y un proceso:

- `switchuvm(p)` prepara el entorno del kernel para ejecutar el proceso `p`.
- `switchkvm()` restaura el entorno general del kernel, usado por el scheduler
  cuando no hay un proceso de usuario seleccionado.
- `swtch(old, new)` está en ensamblador y guarda/restaura los registros que
  permiten continuar la ejecución desde otro contexto de kernel. No elige un
  proceso: sólo realiza el salto una vez que alguien ya lo eligió.

Los detalles de memoria que sustentan `switchuvm()` y `switchkvm()` se verán
en la tarea de administración de memoria. Para esta tarea, trátalas como
operaciones inseparables de la secuencia de cambio de contexto existente.

En `scheduler()`, la secuencia existente ya tiene el orden apropiado:

```text
elegir p con ptable.lock
switchuvm(p)
p->state = RUNNING
swtch(&cpu->scheduler, p->context)
switchkvm()
```

Al implementar una nueva política, cambia la elección de `p`, no esta
secuencia de cambio de contexto.

## Planificación y round-robin

La tabla de procesos no está dividida físicamente por prioridad. Para dos
niveles, ambos recorridos pueden inspeccionar la tabla completa y filtrar por
`p->priority`. Un cursor por prioridad indica desde qué *slot* de
`ptable.proc` se inicia la próxima búsqueda circular.

Un cursor debe avanzar cuando un proceso termina su turno o quantum. Si cada
búsqueda comienza siempre en `ptable.proc[0]`, el primer proceso ejecutable de
un nivel puede monopolizar ese nivel aunque uses una condición de prioridad
correcta.

El contador de quantum y `rtime` responden a preguntas distintas. El primero
se reinicia al agotar un quantum, cambiar de nivel, despertar o recibir un
boost. `rtime`, en cambio, se acumula durante toda la vida del proceso.

## Copiar datos al espacio de usuario

Los punteros recibidos por una syscall provienen de un programa de usuario:
nunca deben desreferenciarse sin validación. `argint()` obtiene argumentos
enteros y `argptr()` valida que un rango completo pertenezca al espacio de
direcciones del proceso llamador.

Para un arreglo de `max` elementos, valida el tamaño de **todo** el arreglo,
no sólo el primer elemento. Trata primero los casos `max < 0` y `max == 0`, y
evita multiplicaciones cuyo resultado pueda desbordar. Luego puedes tomar
`ptable.lock` y copiar una instantánea a `struct procinfo`.

La instantánea no queda congelada al volver al usuario: otro tick o syscall
puede cambiar sus datos inmediatamente. Lo importante es que cada entrada se
extraiga de manera coherente mientras se sostiene el lock.

## Compilación y pruebas

Los programas de usuario sólo llegan a la imagen de xv6 si están en `UPROGS`
en el `Makefile`. Las pruebas entregadas quedan comentadas al inicio para que
el código base compile antes de implementar las syscalls; descoméntalas cuando
corresponda.

Una secuencia útil es:

```sh
make clean
make qemu-nox
```

Dentro de xv6 puedes ejecutar `ps_test`, `schedtest` y `ps`. Para terminar
QEMU en modo no gráfico, usa `Ctrl-A` seguido de `x` en la terminal que lo
ejecuta.

## Errores comunes

- Modificar `p->state` o recorrer `ptable.proc` sin `ptable.lock`.
- Mantener `tickslock` mientras se adquiere `ptable.lock`.
- Contabilizar `wtime` en `SLEEPING`: un proceso bloqueado por E/S no está
  esperando CPU.
- Copiar `struct proc` completo hacia usuario en vez de `struct procinfo`.
- Validar sólo el primer elemento del buffer de una syscall.
- Reiniciar `rtime` al cambiar de prioridad; sólo se reinicia el contador del
  quantum.
- Interpretar el cursor de MLFQ como una frontera física de la tabla, en vez
  de un punto de inicio para una búsqueda circular.
