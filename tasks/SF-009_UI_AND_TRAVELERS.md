# SF-009 — Grid UI and Traveler Visualization

Implement grid editing, Station and Flowpoint editing, logic visualization, transport controls, and Traveler sprites.

Acceptance:
- Visual Travelers mirror engine state.
- Traveler color comes from source Station.
- Direction marker uses source color.
- UI frame drops do not alter sequencing.
- Station movement sends model commands; it does not directly mutate engine-owned runtime memory.
