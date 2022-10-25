SETLOCAL ENABLEDELAYEDEXPANSION
set d=50
set folder=nTest256core%d%d150exact
set pcName=i10pc138
for %%t in (relSpeedup) do (
    for %%n in (100000 300000 500000) do ( Rem  50000 10000
        set cm=python ../scripts/plot/plot_curves.py -xy -xlabel="Anzahl Worker" -ylabel="relativer Speedup" -size="3.75" -title="d=%d% n=%%n"
        for %%k in (10 30 50 100) do ( Rem for /l %%k in (1, 1, 100) do ( Rem 10 30 50 70 100
            set cm=!cm! .\%folder%\%%t-%pcName%-%%k-%%n.txt -l="k=%%k"
        )
        set cm=!cm!  -o=.\%folder%\Graph-%%n-%%t-%pcName%-kn.pdf
        call !cm!
    )
)
pause
