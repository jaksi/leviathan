from setuptools import setup


setup(
    name='leviathan',
    version='0.1.1',
    packages=['leviathan'],
    scripts=['bin/levctl'],
    install_requires=['pyusb'],
)
