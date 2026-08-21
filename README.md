# ZXChat

An AI chatbot that runs on a real ZX Spectrum.

ZXChat installs itself as a BASIC command. You keep writing your program, and
`%chat` is there to talk to, to ask about the code on screen, or to rewrite it
for you. The cartridge talks to the API on its own: nothing else to install,
nothing running on your PC.

```basic
10 FOR i=1 TO 10
20 PRINT 1/(i-5)
30 NEXT i
%chat "why does line 20 crash? fix it"
```
```
Thinking...

At i=5 you divide by zero. Skip
that value by adding this line.

15 IF i=5 THEN GO TO 30

Use /fix to apply it
```
```basic
%chat "/fix"
```
```
Applying...

Program updated. Type LIST.
```

## What it does

| You type | What happens |
|---|---|
| `%chat` | full-screen chat window, with scrollback |
| `%chat "…"` | ask about the program in memory, without leaving BASIC |
| `%chat "/fix"` | apply the changes it suggested |
| `%chat "/write"` | replace the program with the one it wrote |

`/fix` and `/write` are the interesting part: the model writes BASIC, the
Spectrum tokenises it itself and splices it into memory. Type `LIST` and it is
there.

> [!WARNING]
> ZXChat is still experimental. Do not expect awesome results. Also notice that
> using `/fix` might break your loaded BASIC program.

## What you need

- A **ZX Spectrum 48K or 128K** (+2A and +3 are not supported yet)
- A **Spectranext** cartridge, tested on firmware **0.8**
- An API key from **OpenAI**
- The [Spectranext SDK](https://github.com/spectranext/spectranext-sdk), to
  build the tape

Installing the SDK, on each platform:

```bash
# macOS
brew tap spectranext/homebrew-spectranext
brew install spectranext-sdk
```

```bash
# Linux
git clone https://github.com/spectranext/spectranext-sdk
cd spectranext-sdk && ./install.sh
```

```batch
:: Windows
git clone https://github.com/spectranext/spectranext-sdk
cd spectranext-sdk
install.bat
```

## Build it

```bash
# macOS and Linux
./build.sh
```

```batch
:: Windows
build.cmd
```

The wizard asks for the language, the machine, the provider, the model, how
hard it should think, and your API key, and leaves a `zxchat.tap` ready to load. If it finds a cartridge
connected it offers to upload it too.

Then, from BASIC:

```basic
%tapein "zxchat"
LOAD ""
```

## About your API key

> [!WARNING]
> **Your key is compiled into the tape.** Never share or publish the `.tap`
> file unless you want to be charged for other people using ZXChat.

That is the design, not an oversight: the Spectrum has nowhere to store a
secret and no way to type one comfortably, so everyone builds their own tape
with their own key and nobody is paying for anybody else.

The wizard never writes your key anywhere except the header it needs to
compile, and deletes it as soon as the build ends — including when the build
fails. It is not saved, not cached, and not sent anywhere except to the
provider you chose.

## FAQ

**The program returned by ZXChat is not working properly. Why?**

Sometimes models return BASIC in a non-Spectrum dialect, which will fail to
run. A larger model gets it right more often, and so does more reasoning
effort — the build wizard asks for both. Both cost you waiting time, which on a
Spectrum you feel.

**I asked something long and got no answer at all. Why?**

Almost certainly reasoning effort. `max_completion_tokens` counts the thinking
as well as the reply, so at medium or high the model could spend the whole
budget reasoning and return an empty answer — which on a Spectrum looks exactly
like a crash. The ceiling is now 16000, well past the ~4000 medium needs and
the ~8000 high needs, and an empty answer says so on screen instead of showing
nothing.

**Can I use tools or web search?**

No. ZXChat only talks: it cannot look anything up or run anything on your
behalf.

**Why is the output a `.tap` file?**

Because that is how a Spectrum loads code. A `.tap` is a tape image, and the
cartridge can serve one as a virtual tape, which is what `%tapein` does.

**Does the file on the cartridge need the `.tap` extension?**

No, and it is worth dropping. `%tapein` does not care about the extension, so
uploading it as plain `zxchat` is four characters less to type every time. The
wizard does this for you.

**After loading, I see a strange line 10 in BASIC. Can I remove it?**

That is the loader that came inside the tape. Type `10` and press ENTER to
delete it, like any other BASIC line. To avoid it appearing at all, load the
tape in one line, in direct mode:

```basic
CLEAR VAL "32767": %tapein "zxchat": LOAD ""CODE : RANDOMIZE USR VAL "32768"
```

## Licence

MIT. See [LICENSE](LICENSE).
