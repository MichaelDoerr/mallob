SETLOCAL ENABLEDELAYEDEXPANSION
set folder=dTest256core
set pcName=i10pc138
for %%t in (times relSpeedup efficiency) do (
    set cm=python ../scripts/plot/plot_curves.py -xy
    for %%k in (10 30 50) do ( Rem for /l %%k in (1, 1, 100) do (
        for %%n in (100000 300000 500000) do ( Rem  50000 10000
            set cm=!cm! .\%folder%\%%t-%pcName%-%%k-%%n.txt -l="k=%%k n=%%n"
        )
    )
    set cm=!cm!  -o=.\%folder%\Graph-%%t-%pcName%-kn.pdf
    call !cm!
)
pause
