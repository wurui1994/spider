#include "spider.h"

xpath_result_t print_xpath_nodes(xmlNodeSetPtr nodes)
{
    xmlNodePtr cur;
    // assert(output);
    size_t size = (nodes) ? (size_t)nodes->nodeNr : 0;

    // fprintf(output, "Result (%d nodes):\n", size);
    char **get = (char **)malloc(size * sizeof(char *));
    for (size_t i = 0; i < size; ++i)
    {
        get[i] = (char *)malloc(sizeof(char));
        assert(nodes->nodeTab[i]);
        cur = (xmlNodePtr)nodes->nodeTab[i];
        // printf("key : %s \n value : %s\n", cur->name, xmlNodeGetContent(cur));
        get[i] = (char *)xmlNodeGetContent(cur);
    }

    xpath_result_t result = {get, size};

    return result;
}

/**
 * execute_xpath_expression:
 * @filename:		the input XML filename.
 * @xpathExpr:		the xpath expression for evaluation.
 * @nsList:		the optional list of known namespaces in
 *			"<prefix1>=<href1> <prefix2>=href2> ..." format.
 *
 * Parses input XML file, evaluates XPath expression and prints results.
 *
 * Returns 0 on success and a negative value otherwise.
 */
xpath_result_t execute_xpath_expression(const char *filename, const xmlChar *xpathExpr)
{
    xpath_result_t result = {NULL, 0};
    xmlDocPtr doc;
    xmlXPathContextPtr xpathCtx;
    xmlXPathObjectPtr xpathObj;

    assert(filename);
    assert(xpathExpr);

    /* Load XML document */
    // doc = xmlParseFile(filename);

    doc = htmlReadMemory(filename, (int)strlen(filename), NULL, NULL,
                         HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    // doc = htmlDocPtr(filename, NULL);
    if (doc == NULL)
    {
        fprintf(stderr, "Error: unable to parse this string\n");
        return result;
    }

    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL)
    {
        fprintf(stderr, "Error: unable to create new XPath context\n");
        xmlFreeDoc(doc);
        return result;
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if (xpathObj == NULL)
    {
        fprintf(stderr, "Error: unable to evaluate xpath expression \"%s\"\n",
                xpathExpr);
        xmlXPathFreeContext(xpathCtx);
        xmlFreeDoc(doc);
        return result;
    }

    /* Print results */
    result = print_xpath_nodes(xpathObj->nodesetval);

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);

    return result;
}

/**
 * xpath :
 * @xml : the input string
 * @path : the rules
 * @get : array of string
 * @num : size of @get

 * return the number of string we get
 **/
xpath_result_t xpath(char *xml, char *path)
{
    xpath_result_t result = {NULL, 0};
    /* Init libxml */
    xmlInitParser();

    /* Do the main job */
    result = execute_xpath_expression(xml, BAD_CAST path);
    if (result.size == 0)
    {
        return result;
    }

    /* Shutdown libxml */
    xmlCleanupParser();

    /*
   * this is to debug memory for regression tests
   */
    // xmlMemoryDump();
    return result;
}

/*
int main()
{
   char *xml = "<doc href=\"jb\"><ni>one</ni><ni>two</ni></doc>";
   char *path = "/doc/ni";
   char *get[10];
   int i;
   int size = xpath(xml, path, get);
   for (i = 0; i < size; i++)
     printf("%s\n", get[i]);
   return 0;
 }
*/
