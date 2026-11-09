for s in flirv_4FRnNp flirv_5RSrbW flirv_6tLtjd flirv_SCiKdG flirv_ePoikf flirv_vbrSzr flirstill_val; do
  ./bm_eval $(ls ${EWC_DATA:?set EWC_DATA}/${s}_640x512*.u16 | head -1) 640 512 10 8333 $1 rec/bm_$1_$s.u16 rec/bm_$1_$s.csv
done
