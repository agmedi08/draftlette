## Clock text and the layout of the status bar's clock and battery slots,
## shared by editor_ui and planner_ui. No UI, and plain C, so host_test/
## can run it on a computer.

idf_component_register(
    SRCS "status_bar.c"
    INCLUDE_DIRS "include"
)
