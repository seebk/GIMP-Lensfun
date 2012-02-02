
See http://lensfun.sebastiankraft.net for detailed install 
instructions and documentation.


INSTALLATION:
############

Unzip the archive:

>$ tar -xvzf gimplensfun-0.x.x.tar.gz

Binaries can then be found under gimplensfun-0.x/bin/linux/. 
Select the architecture for your system (i386/amd64) and copy
the binary file "gimplensfun" to your GIMP plugin folder. 
Normally you this located at "/home/USER/.gimp2.6/plug-ins/".
You need to restart GIMP to detect the new plugin.


COMPILATION:
###########

Dependencies:
- libgimp2.0
- libexiv2
- liblensfun

On (K)Ubuntu you can easily install the required libs by 

>$ sudo apt-get install libgimp2.0-dev libexiv2-dev liblensfun-dev

Afterwards unzip the sources:

>$ tar -xvzf gimplensfun-0.x.x.tar.gz

Enter the directory and compile with "make":

>$ cd gimplensfun-0.x.x
>$ make

If all went fine copy the newly created binary file "gimplensfun" 
to your GIMP plugins folder. 
Normally you find this at "/home/USER/.gimp2.6/plug-ins/".

You need to restart GIMP to detect the new plugin.

