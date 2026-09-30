# Commands, privacy and scripts

## Which commands are there?

Type `/` to see them all. Commands forgive typos: `/mdl`, `/modle` and `/mod` all find
`/model`.

- `/model` pick the model, add a local server, or log in
- `/armory` open or create an armory
- `/add` copy a file or folder into this armory
- `/new` start a new chat
- `/sources` show the passages behind the last answer
- `/login` and `/logout` add or remove a hosted provider
- `/help` list the commands and keys; `/exit` or Ctrl+D quit

In the `/` menu, Up and Down move, Tab completes, Enter runs and Esc closes it.

## Which keys are there?

- Esc stops an answer while it is being written.
- Ctrl+C clears the line; on an empty line, press it twice to quit. Ctrl+D quits too.
- Ctrl+L opens the model picker.
- Ctrl+O shows the passages behind the last answer, in full.
- Ctrl+T shows or hides the model's thinking after each answer.
- Alt+Enter starts a new line. Up and Down bring back earlier questions.

## Is my data private?

Your files stay on your machine. Heph sends only the question, the recent chat and the
matching passages, and only to the model you picked. With a local server nothing leaves
the computer. Heph downloads nothing, runs no plugins, gives the model no tools, and
never opens a browser. The only program it starts is its own search core.

## Can I use Heph from scripts?

Yes:

- `heph ask notes "what is the deadline?"` answers one question from the armory notes.
- `heph ask notes "..." --json` prints the answer, citations and checks as JSON.
- `heph index notes` indexes without asking.
- `heph config` shows the settings and logins in effect.

## Heph cannot reach the model. What now?

Check that the server named under the prompt is running, then type `/model` to see
which logins answer and why the others fail. A local server usually needs to be started
with its API enabled; a hosted login usually needs a valid key.
