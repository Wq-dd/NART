# ART python binding
## build
Just enable desired target by adding it into NART_CASE_MODULES when building nart-switch.
## get started
A simple sample to load and run nart engine, which was generated by nart-switch.
```python
from nart.art import load_parade, create_context_from_json
ctx = create_context_from_json("engine.bin.json")
net = load_parade("engine.bin", ctx)
from nart.art import get_empty_io
inputs, outputs = get_empty_io(net)
# or you can compose inputs/outputs dict by your self.
if net.run(inputs, outputs):
    print("run engine success")
else:
    print("run engine failed")
```
