---
---
## Webview API documentation
Functions and for creating and controlling webviews to show html pages or
evaluate javascript. These API:s only exist on mobile platforms.

## Constants
{% for constant in site.data.api.constants %}
### {{ constant.name }}
{{ constant.desc }}
{% endfor %}

## Functions
{% for function in site.data.api.functions %}
### {{ function.name }}({% for param in function.params %}{{param.name}}{% unless forloop.last %},{% endunless %}{% endfor %})
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
            <td>{{ param.name }}</td>
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
#### Returns
<code>{{ function.return.type }}</code> {{ function.return.desc }}
{% endif %}
{% if function.examples %}
#### Examples
{% for example in function.examples %}
```lua
{{ example.code }}
```
{% endfor %}
{% endif %}
---
{% endfor %}
