## Process State Transition

```
            ┌───────┐   resched    ┌─────────┐
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
                            │
                            │ create_process
                            │
                            │
                            │
```

