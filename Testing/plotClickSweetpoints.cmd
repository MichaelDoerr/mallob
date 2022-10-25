SETLOCAL ENABLEDELAYEDEXPANSION


set cm=python ../scripts/plot/plot_curves.py -linestyles="None,--,-" -markers="^,None,s" -xy -xlabel="Jobgr\\\"o\ss{}e $k\cdot n\cdot d$" -ylabel="bester Demand" -title="Bester Demand f\\\"ur Jobgr\\\"o\ss{}e" -size="3.75"

set cm=!cm! .\FinalSweetpointsPlot.txt -no-legend

set cm=!cm!  -o=.\FinalSweetpointsPlot.pdf
call !cm!

pause
