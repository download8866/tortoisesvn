<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version='1.0'>

  <xsl:output method="xml"
              encoding="UTF-8"
              indent="no"/>

  <xsl:template match="screen|literal|filename|programlisting"/>

  <xsl:template match="node() | @*">
    <xsl:copy>
      <xsl:apply-templates select="@* | node()"/>
    </xsl:copy>
  </xsl:template>
  
</xsl:stylesheet>
