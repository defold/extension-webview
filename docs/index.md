## Webview API documentation
Functions and for creating and controlling webviews to show html pages or
evaluate javascript. These API:s only exist on mobile platforms.

## Functions
<ul>
{% for function in site.data.api %}
    <li>
        <h3>{{ function.name }}</h3>
        {{ function.desc }}

        <table>
            <thead>
                <tr>
                    <th>Parameter</th>
                    <th>Type</th>
                    <th>Desc</th>
                </tr>
            </thead>
            <tbody>
            {% for param in function.params %}
                <tr>
                    <td>{{ param.name }}</td>
                    <td>{{ param.type }}</td>
                    <td>{{ param.desc }}
                        {% if param.type == "function" %}

                        <br>
                        <b>({% for sig in param.sig %}
                        {{ sig.name }}{% unless forloop.last %},{% endunless %}
                        {% endfor %})</b>
                        <table>
                            <thead>
                                <tr>
                                    <th>Parameter</th>
                                    <th>Type</th>
                                    <th>Desc</th>
                                </tr>
                            </thead>
                            <tbody>
                                {% for sig in param.sig %}
                                <tr>
                                    <td>{{ sig.name }}</td>
                                    <td>{{ sig.type }}</td>
                                    <td>{{ sig.desc }}</td>
                                </tr>
                                {% endfor %}
                            </tbody>
                        </table>
                        {% endif %}
                    </td>
                </tr>
                {% endfor %}
            </tbody>
        </table>
    </li>
{% endfor %}
</ul>

<li><b>TYPE:</b> {{ param.type }}<br/>
                {% if param.type == "function" %}
                    THIS IS A FUNCTION
                {% endif %}
                <b>NAME:</b> {{ param.name }}<br/>
                <b>DESC:</b> {{ param.desc }}</li>
