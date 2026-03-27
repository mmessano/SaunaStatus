# sauna-post-edit-validation

Evolved from 2 instincts (avg confidence: 92%)

## When to Apply

Trigger: after editing any source file in SaunaStatus

## Actions

- Run `python3 -m json.tool <file>` immediately after editing any `.json` file.
Never proceed if validation fails — trailing commas and missing brackets are silent killers.
- - C++/.h edit → run `pio run` immediately
- JSON edit → run `python3 -m json.tool <file>` immediately
- Functional change → run `pio test -e native`
Never move on without passing validation.
