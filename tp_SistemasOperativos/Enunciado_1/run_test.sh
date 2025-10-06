#!/bin/bash
# Ejecuta el tp con 3 productores y 50 registros de prueba
./tp 3 50 &
TP_PID=$!
echo "TP pid = $TP_PID"

# Mostrar procesos
ps -ef | grep tp | grep -v grep

# Mostrar memoria compartida (system V)
echo "ipcs -m:"
ipcs -m

# Mostrar /dev/shm (si aplica)
echo "/dev/shm:"
ls -la /dev/shm

# Esperar a que termine
wait $TP_PID
EXIT=$?
echo "tp terminó con código $EXIT"
echo "Últimas 10 líneas de registros.csv:"
tail -n 10 registros.csv
