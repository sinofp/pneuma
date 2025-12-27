## Process State Transition

```                    ┌────────┐
                ┌──────┤sleeping│◄──────┐
                │      └────────┘       │
         wakeup │                       │sleep
                ▼                       │
            ┌───────┐  reschedule  ┌────┴────┐
            │ READY │◄────────────►│ RUNNING │
            └───┬───┘              └────┬────┘
                │  ▲                    │
                │  │                    │
        suspend │  │ resume             │ suspend
                │  └────────┐           │
                │           │           │
                │     ┌─────┴─────┐     │
                └────►│ SUSPENDED │◄────┘
                      └───────────┘
                            ▲
                            │
                            │ create_process
                            │
                            │
```

