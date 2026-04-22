TPS
"<?xml version=""1.0"" encoding=""utf-8""?>
<TpsData xmlns:xsd=""http://www.w3.org/2001/XMLSchema"" xmlns:xsi=""http://www.w3.org/2001/XMLSchema-instance"">
  <Name>flac</Name>
  <!-- Software Name and Version  -->
<!-- Software Name: flac
    Download Link: https://xiph.org/flac/ https://ftp.osuosl.org/pub/xiph/releases/flac/flac-1.5.0.tar.xz
    Version: 1.5.0
    Notes: This isn't an 'online' feature, but perhaps it could be used to read from a network stream, I'm not sure.

We have a flac 1.4.3 tps already.

I'm not sure exactly which of the dozens of bsd licences this one is under.  It says 'bsd' but then it links to a file at https://github.com/xiph/flac/blob/master/COPYING.Xiph.  It looks like the 'three clause' licence to me.

We are using only the lib, not the executables which are GPL licenced.
        -->
<Location>//Fortnite/Main</Location>
<Function>FLAC stands for Free Lossless Audio Codec, an audio format similar to MP3</Function>
<Eula>https://xiph.org/flac/license.html</Eula>
  <RedistributeTo>
<EndUserGroup></EndUserGroup>
    <EndUserGroup>P4</EndUserGroup>
    <EndUserGroup>Git</EndUserGroup>
  </RedistributeTo>
  <LicenseFolder>/Engine/Source/ThirdParty/Licenses</LicenseFolder>
</TpsData>
 "