
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

  >$ sudo apt-get install build-essential libgimp2.0-dev libexiv2-dev liblensfun-dev git

On Fedora 15 (and probably other versions, too) you need to 
install these packages:

  gcc, gcc-c++, gimp-devel-tools, lensfun-devel, exiv2-devel, git

Afterwards get the sources from github:

  >$ git clone git://github.com/seebk/GIMP-Lensfun.git

Enter the directory and compile with "make":

  >$ cd gimplensfun-0.x.x
  >$ make

If all went fine copy the newly created binary file "gimplensfun" 
to your GIMP plugins folder. 
Normally you find this at "/home/USER/.gimp2.6/plug-ins/".

You need to restart GIMP to detect the new plugin.

