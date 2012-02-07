<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
    <xsl:output method="xml" omit-xml-declaration="no" encoding="UTF-8" doctype-system="http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd" indent="yes" />

 <xsl:template match="node()|@*">
     <xsl:copy>
       <xsl:apply-templates select="node()|@*"/>
     </xsl:copy>
 </xsl:template>

 <xsl:template match="chapter">
   <xsl:copy>
     <xsl:apply-templates select="title|para|@*"/>
     <xsl:apply-templates select="refentry">
         <xsl:sort select="refnamediv/refname"/>
     </xsl:apply-templates>
   </xsl:copy>
 </xsl:template>
</xsl:stylesheet>
