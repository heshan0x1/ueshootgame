<?xml version="1.0" encoding="utf-8"?>
<TpsData xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Name>mpg123</Name>
  <!-- Software Name and Version  -->
<!-- Software Name: mpg123
    Version: 1.32.10 
	Because mpg123 is LPGL licenced we must either not modify the source and link it as a dll OR release everything needed to build under LPGL.
	In this implementation I have linked mpg123 into libsndfile as a dll, and no source changes were needed.  So we are complying in that way.
	I believe that because we link libsndfile as a dll we could link mpg123 statically into libsndfile and then release our libsndfile source/build scripts/etc, 
	to comply and that this would be an OK solution for Epic (we are really just building libsndfile for some specific open platforms) however we would need to do 
	another TPS legal review to confirm that plan.-->
  <Location>/Engine/Binaries/ThirdParty/libsndfile/</Location>
  <Function>mpg123 decodes and encodes MPEG audio.</Function>
  <Eula></Eula>
  <RedistributeTo>
    <EndUserGroup>Licencees</EndUserGroup>
    <EndUserGroup>P4</EndUserGroup>
    <EndUserGroup>Git</EndUserGroup>
  </RedistributeTo>
  <LicenseFolder>/Engine/Source/ThirdParty/Licenses</LicenseFolder>
</TpsData>
 


 