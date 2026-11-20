# tickets — the work queue

One job: hold every unit of work, with its state encoded by which folder it sits in.

| Folder | Meaning |
|---|---|
| `01_backlog/` | not started; numbered in intended order |
| `02_active/` | being worked on (keep it to 1–2 tickets) |
| `03_done/` | accepted by a human |

New ticket = copy `_templates/ticket.md`, next free `RT-NNN` number, into `01_backlog/`.

## Inputs (when working a ticket)
- Working: the ticket file itself
- Reference: `../plan/PLAN.md` — only the section the ticket names
- Code: only the files listed under the ticket's **Touches**

Do NOT load: other tickets, `03_done/` history, all of `src/`.

## Process
1. `mv` the ticket into `02_active/`.
2. Implement until every **Acceptance** line holds. Add a check to `tests/` for non-trivial logic.
3. Append a short **Notes** section to the ticket: what was done, anything deferred (`ponytail:` comments in code).

## Outputs
- Code changes + the updated ticket file.

## Human check
Run the acceptance commands, look at the render. Approve → `mv` to `03_done/`. Edit the ticket if scope changed.
