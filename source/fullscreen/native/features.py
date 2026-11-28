"""Shared RHMI and Screen controls for the embedded feature pages."""

# Each tuple defines the label, action, and full-screen bounds.
MHD = (
    ('Map 1', 240, 26, 45, 140, 69),
    ('Map 2', 241, 176, 45, 140, 69),
    ('Map 3', 242, 326, 45, 140, 69),
    ('Map 4', 243, 476, 45, 140, 69),
    ('Exhaust open', 244, 26, 123, 188, 26),
    ('Exhaust auto', 245, 224, 123, 188, 26),
    ('Exhaust closed', 246, 422, 123, 194, 26),
    ('Max cooling off', 247, 26, 155, 286, 26),
    ('Max cooling on', 248, 324, 155, 292, 26),
)
BEAMER = (
    ('Front', 250, 26, 160, 109, 32),
    ('Rear', 251, 144, 160, 109, 32),
    ('Both', 252, 262, 160, 109, 32),
    ('Insane', 253, 380, 160, 109, 32),
    ('Stop', 254, 498, 160, 118, 32),
)

UPDATES = (
    ('Update installed apps', 260, 121, 73, 237, 64),
    ('Install new apps', 261, 370, 73, 246, 64),
)

BOOST_SETTINGS = (
    ('Pressure', ('psi', 'bar', 'kPa')),
    ('Speed', ('mph', 'km/h')),
    ('Temperature', ('Fahrenheit', 'Celsius')),
    ('Gauge style', ('Bars', 'Circular dials')),
    ('Alert mode', ('Screen + soft chime', 'Screen only')),
    ('Coolant high', ('Off', *(f'{temp} °F' for temp in range(220, 261, 5)))),
    ('Oil temp high', ('Off', *(f'{temp} °F' for temp in range(220, 261, 5)))),
    ('Oil pressure low', ('Off', *(f'{pressure} psi' for pressure in range(5, 26, 5)))),
    ('Gauge color', ('BMW Orange', 'Blue', 'Green', 'White', 'Purple', 'Pink', 'Cyan', 'Red', 'Multicolor')),
    ('Engine profile', ('Gasoline', 'Diesel')),
)
