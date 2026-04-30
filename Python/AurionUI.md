# AurionUI - AurionOS GUI Framework for Python

AurionUI is the official GUI framework for building desktop applications in Python on AurionOS. It mirrors the familiar patterns of tkinter while adding deep OS integration, real-time graphics, and native AurionOS theming.

## Quick Example

```python
import aurionui as ui

# Create main window
win = ui.Window("My App", width=800, height=600)

# Add a label
label = ui.Label(win, text="Hello, AurionOS!")
label.pack(padx=10, pady=5)

# Add a button
def on_click():
    label.config(text="Button clicked!")

button = ui.Button(win, text="Click Me", command=on_click)
button.pack(padx=10, pady=5)

# Run the app
win.mainloop()
```

## Window

The `Window` class is the main application container.

```python
win = ui.Window(title="My App", width=1024, height=768, resizable=True)
win.configure(bg="#1c1c1e")
win.set_icon("icons/myapp.bmp")
win.set_menu(ui.MenuBar(win, [
    ui.Menu("File", [
        ui.MenuItem("New", command=new_file),
        ui.MenuItem("Open...", command=open_file),
        ui.MenuSeparator(),
        ui.MenuItem("Exit", command=win.destroy)
    ]),
    ui.Menu("Help", [
        ui.MenuItem("About", command=show_about)
    ])
]))
win.mainloop()
```

### Window Methods

| Method | Description |
|--------|-------------|
| `mainloop()` | Start the application event loop |
| `destroy()` | Close the window and exit |
| `update()` | Force a UI refresh |
| `configure(**kwargs)` | Set window properties |
| `set_icon(path)` | Set window icon |
| `set_menu(menu)` | Attach a menu bar |
| `set_title(title)` | Change window title |
| `center()` | Center window on screen |
| `maximize()` | Maximize window |
| `minimize()` | Minimize to taskbar |

### Window Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `title` | str | "Window" | Window title |
| `width` | int | 800 | Window width in pixels |
| `height` | int | 600 | Window height in pixels |
| `resizable` | bool | True | Allow resize |
| `bg` | str | "#1c1c1e" | Background color |
| `fg` | str | "#f5f5f7" | Foreground color |
| `opacity` | float | 1.0 | Window opacity (0.0-1.0) |

---

## Widgets

### Label

Display static text.

```python
label = ui.Label(win, text="Hello", font=("Arial", 14), fg="#ffffff", bg="#2c2c2e")
label.pack(fill=ui.X, padx=10, pady=5)
```

### Button

Clickable button with command callback.

```python
def save_callback():
    print("Save clicked!")

btn = ui.Button(win, text="Save", command=save_callback, width=100, height=40)
btn.pack(side=ui.LEFT, padx=5)
```

### Entry

Single-line text input.

```python
entry = ui.Entry(win, text="default text", width=50, placeholder="Enter name...")
entry.pack(padx=10, pady=5)

# Get/set value
value = entry.get()
entry.set("new value")
```

### TextBox

Multi-line text editor.

```python
text = ui.TextBox(win, width=60, height=20)
text.insert("1.0", "Hello\nWorld!")
text.pack(fill=ui.BOTH, expand=True, padx=10, pady=5)

# Get all text
content = text.get("1.0", ui.END)
```

### Checkbutton

A checkbox toggle.

```python
checked = ui.BooleanVar()
cb = ui.Checkbutton(win, text="Enable feature", variable=checked)
cb.pack(padx=10, pady=5)

if checked.get():
    print("Feature enabled!")
```

### Radiobutton

Radio button group.

```python
option = ui.IntVar()
r1 = ui.Radiobutton(win, text="Option 1", variable=option, value=1)
r2 = ui.Radiobutton(win, text="Option 2", variable=option, value=2)
r1.pack(); r2.pack()
```

### Listbox

Selectable list.

```python
lb = ui.Listbox(win, height=10)
for item in ["Apple", "Banana", "Cherry"]:
    lb.insert(ui.END, item)
lb.pack(padx=10, pady=5)

# Handle selection
def on_select(event):
    idx = lb.curselection()
    if idx: print(lb.get(idx[0]))

lb.bind("<<ListboxSelect>>", on_select)
```

### Canvas

Custom drawing surface.

```python
canvas = ui.Canvas(win, width=400, height=300, bg="#000000")
canvas.pack(padx=10, pady=5)

# Draw shapes
canvas.create_line(0, 0, 400, 300, fill="#ff0000", width=2)
canvas.create_rectangle(50, 50, 150, 150, fill="#00ff00", outline="#ffffff")
canvas.create_oval(200, 100, 350, 250, fill="#0000ff")
canvas.create_text(200, 150, text="Hello Canvas", fill="#ffffff")
canvas.create_image(50, 50, image="icons/photo.bmp")
canvas.create_polygon([100,200, 150,250, 100,300], fill="#ff00ff")
```

### Frame

Container for grouping widgets.

```python
frame = ui.Frame(win, bg="#2c2c2e", padx=10, pady=10)
frame.pack(fill=ui.X, padx=10, pady=5)

inner_label = ui.Label(frame, text="Inside frame")
inner_label.pack()
```

### ProgressBar

Show progress of an operation.

```python
pb = ui.ProgressBar(win, length=300, mode=ui.DETERMINATE)
pb.pack(padx=10, pady=5)
pb.set(0.5)  # 50%

# Indeterminate mode
pb2 = ui.ProgressBar(win, mode=ui.INDETERMINATE)
pb2.start()
```

### Slider

Horizontal or vertical slider.

```python
value = ui.DoubleVar()
slider = ui.Slider(win, from_=0, to=100, orient=ui.HORIZONTAL, variable=value)
slider.pack(padx=10, pady=5)
print(value.get())
```

### Spinbox

Numeric input with up/down arrows.

```python
val = ui.IntVar()
sp = ui.Spinbox(win, from_=0, to=100, textvariable=val)
sp.pack(padx=10, pady=5)
```

### Combobox

Dropdown selection.

```python
cb = ui.Combobox(win, values=["Option A", "Option B", "Option C"], state="readonly")
cb.pack(padx=10, pady=5)
cb.bind("<<ComboboxSelected>>", lambda e: print(cb.get()))
```

### Treeview

Hierarchical data display.

```python
tv = ui.Treeview(win, columns=("Name", "Size"), show="tree headings")
tv.heading("#0", text="Type")
tv.heading("Name", text="Name")
tv.heading("Size", text="Size")
tv.insert("", ui.END, text="Folder", values=("MyFolder", ""))
tv.insert("", ui.END, text="File", values=("readme.txt", "1KB"))
tv.pack(fill=ui.BOTH, expand=True)
```

### Tabview

Tabbed interface.

```python
tabs = ui.Tabview(win)
tab1 = tabs.add("Tab 1")
tab2 = tabs.add("Tab 2")
ui.Label(tab1, text="Content of tab 1").pack()
ui.Label(tab2, text="Content of tab 2").pack()
tabs.pack(fill=ui.BOTH, expand=True)
```

### Messagebox

Show dialog boxes.

```python
ui.messagebox.showinfo("Info", "This is an info message")
ui.messagebox.showwarning("Warning", "This is a warning")
ui.messagebox.showerror("Error", "Something went wrong!")
result = ui.messagebox.askyesno("Confirm", "Continue?")
```

### Filedialog

Open/save file dialogs.

```python
# Open file
filename = ui.filedialog.askopenfilename(
    title="Open File",
    filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
)

# Save file
filename = ui.filedialog.asksaveasfilename(
    title="Save As",
    defaultextension=".txt",
    filetypes=[("Text files", "*.txt")]
)

# Select directory
dirname = ui.filedialog.askdirectory(title="Choose Folder")
```

### Dialog

Custom modal dialog.

```python
def show_settings():
    dlg = ui.Dialog(win, title="Settings", modal=True)

    ui.Label(dlg.content, text="Setting 1:").pack()
    entry = ui.Entry(dlg.content)
    entry.pack()

    def on_ok():
        print(entry.get())
        dlg.close()

    ui.Button(dlg.content, text="OK", command=on_ok).pack(side=ui.LEFT)
    ui.Button(dlg.content, text="Cancel", command=dlg.close).pack(side=ui.LEFT)
    dlg.show()
```

---

## Geometry Management

All widgets use the `pack()` geometry manager.

```python
widget.pack(
    side=ui.TOP,        # TOP, BOTTOM, LEFT, RIGHT
    fill=ui.NONE,       # NONE, X, Y, BOTH
    expand=True,        # Expand to fill extra space
    anchor=ui.CENTER,   # N, S, E, W, NE, NW, SE, SW, CENTER
    padx=0,             # External padding
    pady=0,
    ipadx=0,            # Internal padding
    ipady=0
)
```

### Grid Layout

Alternative geometry manager using rows and columns.

```python
ui.Label(win, text="Name:").grid(row=0, column=0, sticky=ui.E, padx=5, pady=5)
ui.Entry(win, width=30).grid(row=0, column=1, columnspan=2, padx=5, pady=5)
ui.Label(win, text="Age:").grid(row=1, column=0, sticky=ui.E, padx=5, pady=5)
ui.Entry(win, width=5).grid(row=1, column=1, padx=5, pady=5)
```

---

## Events and Bindings

### Binding Events

```python
def on_key(event):
    print(f"Key pressed: {event.keysym}")

def on_click(event):
    print(f"Clicked at {event.x}, {event.y}")

def on_double_click(event):
    print("Double clicked!")

entry.bind("<KeyPress>", on_key)
canvas.bind("<Button-1>", on_click)
canvas.bind("<Double-Button-1>", on_double_click)
canvas.bind("<Motion>", lambda e: coords.config(text=f"X:{e.x} Y:{e.y}"))
```

### Common Events

| Event | Description |
|-------|-------------|
| `<Button-1>` | Left mouse click |
| `<Double-Button-1>` | Double left click |
| `<Button-3>` | Right mouse click |
| `<Motion>` | Mouse movement |
| `<KeyPress>` | Any key press |
| `<KeyRelease>` | Key release |
| `<Enter>` | Widget focus enter |
| `<Leave>` | Widget focus leave |
| `<FocusIn>` | Widget gets focus |
| `<FocusOut>` | Widget loses focus |
| `<Configure>` | Widget resized/moved |
| `<<ListboxSelect>>` | Listbox selection changed |
| `<<ComboboxSelected>>` | Combobox selection changed |

### Key Names

```python
"<Return>"      # Enter key
"<Escape>"      # Escape key
"<Tab>"         # Tab key
"<BackSpace>"   # Backspace
"<Delete>"      # Delete key
"<space>"       # Spacebar
"<Home>"        # Home key
"<End>"         # End key
"<Left>"        # Arrow keys
"<Right>"
"<Up>"
"<Down>"
"<Control-a>"   # Ctrl+A
"<Alt-f>"       # Alt+F
```

---

## Variables

Widgets can be linked to variable objects for two-way data binding.

```python
# String variable
text_var = ui.StringVar()
text_var.set("Hello")
text_var.get()  # "Hello"

# Integer variable
int_var = ui.IntVar()
int_var.set(42)
int_var.get()  # 42

# Double variable
float_var = ui.DoubleVar()
float_var.set(3.14)

# Boolean variable
bool_var = ui.BooleanVar()
bool_var.set(True)

# Link to widget
entry = ui.Entry(win, textvariable=text_var)
check = ui.Checkbutton(win, variable=bool_var)
radio = ui.Radiobutton(win, variable=int_var, value=1)
```

---

## Fonts

```python
font = ui.Font(family="Arial", size=12, weight=ui.BOLD, slant=ui.ITALIC, underline=True)
label = ui.Label(win, text="Styled", font=font)

# Available weights: NORMAL, BOLD
# Available slants: ROMAN, ITALIC
```

---

## Colors

AurionUI accepts color names or hex strings.

```python
# Hex colors
ui.Label(win, text="Red", fg="#ff0000")
ui.Label(win, text="Blue", fg="#0000ff")

# Named colors
ui.Label(win, text="White", fg="white", bg="black")

# AurionOS theme colors
ACCENT = "#0a84ff"       # macOS accent blue
DARK_BASE = "#1c1c1e"    # Material dark base
SURFACE = "#2c2c2e"       # Elevated surface
TEXT_PRIMARY = "#f5f5f7"
TEXT_SECONDARY = "#aeaeb2"
```

---

## Images

```python
# Load image
img = ui.Image("icons/logo.bmp")
img2 = ui.Image("icons/photo.png")

# Display on label or button
lbl = ui.Label(win, image=img)
lbl.pack()

# Resize image
img_resized = img.resize(100, 100)
```

---

## Async and Real-time

AurionUI supports asynchronous operations and real-time updates.

```python
import asyncio

async def long_task():
    for i in range(100):
        await asyncio.sleep(0.1)
        progress.set(i / 100)
        win.update()

# Run async function
ui.run_async(long_task)
```

### Real-time Updates

```python
def update_clock():
    from datetime import datetime
    clock.config(text=datetime.now().strftime("%H:%M:%S"))
    win.after(1000, update_clock)

clock = ui.Label(win, text="")
update_clock()
```

---

## AurionOS Integration

AurionUI provides deep access to AurionOS features.

```python
import aurionui
import aurionui.os

# Get system info
info = aurionui.os.get_system_info()
print(f"CPU: {info['cpu']}")
print(f"Memory: {info['memory_total']}")

# Get processes
procs = aurionui.os.get_processes()
for p in procs:
    print(f"{p['name']}: {p['cpu']}%")

# Get window list
windows = aurionui.os.get_windows()
for w in windows:
    print(f"Window: {w['title']} ({w['pid']})")

# Control windows
aurionui.os.set_window_foreground(windows[0]['pid'])
aurionui.os.minimize_window(windows[0]['pid'])
```

### aurionui.audio Module

```python
import aurionui.audio

# Play sound
aurionui.audio.play("sounds/notify.wav")

# Play MP3
player = aurionui.audio.MP3Player()
player.load("music/song.mp3")
player.play()

# Control playback
player.pause()
player.resume()
player.stop()
player.set_volume(0.5)

# Get current position
pos = player.get_position()
duration = player.get_duration()
```

### aurionui.graphics Module

```python
import aurionui.graphics

# Create 2D context
ctx = aurionui.graphics.Context(400, 300)
ctx.clear("#000000")
ctx.set_color("#ff0000")
ctx.draw_line(0, 0, 400, 300, 2)
ctx.draw_rect(50, 50, 100, 100, fill="#00ff00")
ctx.present()
```

---

## Building Libraries

You can build Python modules into AurionOS-compatible `.a` library files:

```bash
python -m aurionui build mymodule.py
```

This compiles the module to a static library that links into the AurionOS kernel.

---

## Standard Library Modules

AurionOS Python includes these built-in modules:

| Module | Description |
|--------|-------------|
| `os` | Operating system interface |
| `sys` | System-specific parameters |
| `time` | Time access and conversions |
| `datetime` | Date and time classes |
| `math` | Mathematical functions |
| `random` | Random number generation |
| `json` | JSON encoding/decoding |
| `re` | Regular expressions |
| `collections` | Container datatypes |
| `itertools` | Iterator functions |
| `functools` | Higher-order functions |
| `io` | I/O operations |
| `fs` | AurionOS filesystem |
| `aurionui` | GUI framework |
| `aurionui.audio` | Audio playback |
| `aurionui.graphics` | 2D graphics |
| `aurionui.os` | OS integration |

## Python 3.14 Syntax Support

AurionUI supports all Python 3.14 syntax including:

- Template strings: `t'Hello {name}'`
- Deferred annotations: `def foo(x: int) -> str: ...`
- Pattern matching: `match x: case 1: ...`
- Exception groups: `except* KeyError: ...`
- Walrus operator: `if (n := len(data)) > 0: ...`
- F-strings with debugging: `f'{x=}'`
- Structural pattern matching
- Type parameter syntax: `def func[T](x: T) -> T: ...`

Example with modern syntax:

```python
from collections import namedtuple
from dataclasses import dataclass
from typing import Protocol

@dataclass
class Point:
    x: float
    y: float

    def distance(self, other: Point) -> float:
        return ((self.x - other.x)**2 + (self.y - other.y)**2)**0.5

p1 = Point(x=1.0, y=2.0)
p2 = Point(x=4.0, y=6.0)
print(f"Distance: {p1.distance(p2):.2f}")
```
