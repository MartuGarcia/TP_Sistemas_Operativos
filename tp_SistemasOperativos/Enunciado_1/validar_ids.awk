#!/usr/bin/awk -f
# Uso: tail -n +2 registros.csv | cut -d',' -f1 | sort -n | awk -f validar_ids.awk

BEGIN { prev = -1; dup = 0; gaps = 0; count = 0; }
{
  id = $1 + 0;
  count++;
  if(prev == -1){
    prev = id;
    first = id;
    next;
  }
  if(id == prev){
    dup++;
    print "Duplicado:", id;
  } else if (id == prev + 1){
    # OK
  } else if (id > prev + 1){
    gaps++;
    print "Salto entre", prev, "y", id;
  } else if (id < prev){
    print "ID no ordenado (posible problema):", id, "aparece después de", prev;
  }
  prev = id;
}
END {
  if(dup==0 && gaps==0){
    print "IDs OK: sin duplicados y sin saltos detectados (ordenados).";
  } else {
    print "Resumen: duplicados=", dup, " saltos=", gaps, " total_ids=", count;
  }
}

