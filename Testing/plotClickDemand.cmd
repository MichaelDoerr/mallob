SETLOCAL ENABLEDELAYEDEXPANSION
set folder=maxDemandDefaultTest138Version5
set pcName=i10pc138
set cores=128

set cm=python ../scripts/plot/plot_curves.py -nomarkers -linestyles="-,--" -xy -xlabel="Sekunden" -ylabel="Gel\\\"oste Jobs" -title="%cores% Kerne, 6 Jobs" -size="3.75"

for %%t in (Restricted) do (
    set cm=!cm! .\%folder%\cdf-runtimes%%t.txt -l="Beschr\\\"ankt"
)

for %%t in (Unrestricted) do (
    set cm=!cm! .\%folder%\cdf-runtimes%%t.txt -l="Unbeschr\\\"ankt"
)

set cm=!cm!  -o=.\%folder%\Graph.pdf
call !cm!

pause
