# sauna-tdd-workflow

Evolved from 2 instincts (avg confidence: 89%)

## When to Apply

Trigger: when writing or running firmware tests in SaunaStatus

## Actions

- After `pio run` succeeds for any functional change (not just a formatting fix), run:
- 1. Write tests labeled `— red phase` in the commit message
2. Confirm they fail (`pio test -e native`)
3. Then implement the function/module (green phase)
4. Functions for native testing must be inlined in `.h`, not in `.cpp`
