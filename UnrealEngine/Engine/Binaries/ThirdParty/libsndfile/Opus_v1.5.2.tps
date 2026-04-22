TPS
"<?xml version=""1.0"" encoding=""utf-8""?>
<TpsData xmlns:xsd=""http://www.w3.org/2001/XMLSchema"" xmlns:xsi=""http://www.w3.org/2001/XMLSchema-instance"">
  <Name>Opus</Name>
  <!-- Software Name and Version  -->
<!-- Software Name: Opus
    Download Link: https://www.opus-codec.org/   https://ftp.osuosl.org/pub/xiph/releases/opus/opus-1.5.2.tar.gz
    Version: 1.5.2
    Notes: I'm not really sure about the offline/online question.  But maybe the opus audio data could be transmitted over the internet for voice chat or similar?  libsndfile is mostly about loading and saving files with audio data.

We are not using the opusinfo tool, which is under a different licence.

RE: ""redistributed via""... this is used in the editor, not the game, but we distribute the editor as a binary not only as source.

We have an opus 1.4 tps already.  I think this is similar.
        -->
<Location>//Fortnite/Main</Location>
<Function>Opus is an audio codec.  Used to store or transmit sound digitally.</Function>
<Eula>https://www.opus-codec.org/license/</Eula>
  <RedistributeTo>
<EndUserGroup></EndUserGroup>
    <EndUserGroup>P4</EndUserGroup>
    <EndUserGroup>Git</EndUserGroup>
  </RedistributeTo>
  <LicenseFolder>/Engine/Source/ThirdParty/Licenses</LicenseFolder>
</TpsData>
 "
 