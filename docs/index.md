---
layout: default
---
## Constants
{% for constant in site.data.api.constants %}
### <code>{{ constant.name }}</code>
{{ constant.desc }}
{% endfor %}

## Functions
<table>
    <tbody>
{% for function in site.data.api.functions %}
        <tr>
            <td><a href="#{{ function.name | url_encode }}"><code>{{ function.name }}</code></a></td>
            <td>{{ function.short_desc }}</td>
        </tr>
{% endfor %}
    </tbody>
</table>

{% for function in site.data.api.functions %}
<div class="function-wrap">
<h3 class="function-header"><a href="#{{ function.name | url_encode }}" id="{{ function.name | url_encode }}"><code>{{ function.name }}({% for param in function.params %}{{param.name}}{% unless forloop.last %}, {% endunless %}{% endfor %})</code></a></h3>
{{ function.desc }}
<table>
    <thead>
        <tr>
            <th>Parameter</th>
            <th>Type</th>
            <th>Description</th>
        </tr>
    </thead>
    <tbody>
    {% for param in function.params %}
        <tr>
            <td style="text-align: right;"><strong>{{ param.name }}</strong></td>
            <td><code>{{ param.type }}</code></td>
            <td>{{ param.desc }}
                {% if param.type == "function" %}
                {% include type-function.md params=param.params %}
                {% endif %}
                {% if param.type == "table" %}
                {% include type-table.md fields=param.fields %}
                {% endif %}
            </td>
        </tr>
        {% endfor %}
    </tbody>
</table>
{% if function.return %}
<h4>Returns</h4>
<code class="inline-code-block">{{ function.return.type }}</code> {{ function.return.desc }}
{% endif %}
{% if function.examples %}
<h4>Examples</h4>
{% for example in function.examples %}
{% highlight lua %}
{{ example.code }}
{% endhighlight %}
{% endfor %}
{% endif %}
</div>
{% endfor %}
