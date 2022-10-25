SETLOCAL ENABLEDELAYEDEXPANSION


set cm=python ../scripts/plot/plot_curves.py -linestyles="None,--,-" -markers="^,None,s" -xy -xlabel="Jobgr\\\"o\ss{}e $k\cdot n\cdot d$" -ylabel="Demand" -title="Bester Demand f\\\"ur Jobgr\\\"o\ss{}e angepasst" -size="3.75"

set cm=!cm! .\FinalSweetpointsTrimmedPlot.txt -l="angepasste Punkte"
set cm=!cm! .\regression.txt -l="Potenzregression"
set cm=!cm! .\stairs.txt -l="Demand Funktion"

set cm=!cm!  -o=.\FinalSweetpointsTrimmedPlot.pdf
call !cm!

pause
