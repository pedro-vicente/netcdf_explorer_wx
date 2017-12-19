netCDF Explorer
====================

<img src="https://cloud.githubusercontent.com/assets/6119070/11098722/66e4ad1c-886c-11e5-9bd2-097b15457102.png">



netCDF Explorer is a multi-platfrom graphical browser for netCDF files.
[netCDF](http://www.unidata.ucar.edu/software/netcdf)

netCDF support includes
[DAP](http://opendap.org).

Dependencies
------------

[wxWidgets](https://www.wxwidgets.org/)
wxWidgets is a library for creating graphical user interfaces for cross-platform applications.
<br /> 

[netCDF](http://www.unidata.ucar.edu/software/netcdf)
netCDF is a set of software libraries and self-describing, 
machine-independent data formats that support the creation, 
access, and sharing of array-oriented scientific data.
<br /> 

Building from source
------------
GNU autoconf is used


Install dependency packages (Ubuntu):
<pre>
sudo apt-get install build-essential
sudo apt-get install autoconf
sudo apt-get install libwxgtk3.0-dev
sudo apt-get install libnetcdf-dev netcdf-bin netcdf-doc
</pre>

Get source:
<pre>
git clone https://github.com/pedro-vicente/netcdf_explorer_wx.git
</pre>

Build with:
<pre>
autoreconf -vfi
./configure
make
</pre>

Optional for ./configure (when building wxWidgets or netCDF from source):
<pre>
--with-wx-config=PATH   Use the given PATH to wx-config
--with-nc-config=PATH   Use the given PATH to nc-config
</pre>

To generate the included netCDF sample data in /data/netcdf:

<pre>
ncgen -k netCDF-4 -b -o data/test_01.nc data/test_01.cdl
ncgen -k netCDF-4 -b -o data/test_02.nc data/test_02.cdl
ncgen -k netCDF-4 -b -o data/test_03.nc data/test_03.cdl
</pre>

test_01.cdl includes one, two and three dimensional variables with coordinate variables 
<br /> 
test_02.cdl includes a four dimensional variable 
<br /> 
test_03.cdl includes a five dimensional variable
<br /> 

To run and open a sample file from the command line:

<pre>
./netcdf_explorer data/test_03.nc
</pre>

<a target="_blank" href="http://www.space-research.org/">
<img src="https://cloud.githubusercontent.com/assets/6119070/11140582/b01b6454-89a1-11e5-8848-3ddbecf37bf5.png"></a>


